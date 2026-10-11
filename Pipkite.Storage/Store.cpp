#include "Store.h"

#include <algorithm>

#include "Schema.h"
#include "sqlite/sqlite3.h"
#include "Pipkite.Core/JsonModels.h"
#include "Pipkite.Core/ProfitLedger.h"
#include "Pipkite.Core/StateMachine.h"

namespace sr::db {

using nlohmann::json;

namespace {

json parseOr(const std::string& s, json fallback = nullptr) {
    if (s.empty()) return fallback;
    json j = json::parse(s, nullptr, false);
    return j.is_discarded() ? fallback : j;
}

}  // namespace

Result<std::unique_ptr<Store>> Store::open(const std::string& path, const IClock& clock) {
    std::unique_ptr<Store> s(new Store(clock));
    s->path_ = path;
    if (auto st = s->writer_.open(path); !st) return st.error();
    if (auto st = migrate(s->writer_, clock.nowMs()); !st) return st.error();
    s->writer_.exec("PRAGMA wal_autocheckpoint=0;");  // checkpoints run when the writer is idle (writerLoop)
    sqlite3_wal_hook(
        s->writer_.handle(),
        [](void* ctx, sqlite3*, const char*, int frames) -> int {
            static_cast<Store*>(ctx)->walFrames_ = frames;
            return SQLITE_OK;
        },
        s.get());
    if (auto st = s->reader_.open(path); !st) return st.error();
    s->thread_ = std::thread([p = s.get()] { p->writerLoop(); });
    return s;
}

Store::~Store() {
    {
        std::lock_guard lock(qmu_);
        stopping_ = true;
    }
    qcv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

std::string Store::lastWriteError() const {
    std::lock_guard lock(errMu_);
    return lastError_;
}

void Store::writerLoop() {
    // Critical writes (t.done set) commit with synchronous=FULL. Background writes use NORMAL: in WAL mode
    // they are crash-safe for the application and become power-loss durable with the next FULL commit,
    // because that commit's WAL flush covers every earlier frame. So they never add a flush in front of
    // an order journal commit.
    bool fullSync = true;
    for (;;) {
        Task t;
        {
            std::unique_lock lock(qmu_);
            // Idle maintenance, after a few quiet milliseconds and never while a write is waiting:
            //  * background (NORMAL) frames are made durable with one tiny FULL commit, so a later order
            //    commit only flushes its own pages;
            //  * the WAL is checkpointed only once it is large, because the first commit after a WAL
            //    restart is expensive (auto-checkpointing is disabled for the same reason).
            while (!stopping_ && queue_.empty() && background_.empty() && maintenanceDue_) {
                if (qcv_.wait_for(lock, std::chrono::milliseconds(15), [&] { return stopping_ || !queue_.empty() || !background_.empty(); })) break;
                lock.unlock();
                // Checkpoint, then one tiny FULL commit. The commit restarts the WAL at its beginning (inside
                // space the file already owns) and makes all background frames durable, so the next order
                // commit neither extends the WAL file nor flushes other data.
                writer_.exec("PRAGMA wal_checkpoint(PASSIVE);");
                if (!fullSync) writer_.exec("PRAGMA synchronous=FULL;");
                fullSync = true;
                writer_.exec("INSERT INTO settings(key,value,updated_at) VALUES('journal.idleSync','1',0) ON CONFLICT(key) DO UPDATE SET updated_at=updated_at+1;");
                lock.lock();
                walDirty_ = false;
                maintenanceDue_ = false;
            }
            qcv_.wait(lock, [&] { return stopping_ || !queue_.empty() || !background_.empty(); });
            if (queue_.empty() && background_.empty()) return;  // stopping and drained
            // Background writes become eligible 2 ms after queuing: the order commit that usually follows a
            // signal within ~0.1 ms therefore runs first instead of behind them.
            while (!stopping_ && queue_.empty() && !background_.empty()) {
                const auto due = background_.front().queuedAt + std::chrono::milliseconds(2);
                if (std::chrono::steady_clock::now() >= due) break;
                qcv_.wait_until(lock, due, [&] { return stopping_ || !queue_.empty(); });
            }
            // Critical (order-journal) writes never wait behind background flushes.
            auto& q = !queue_.empty() ? queue_ : background_;
            t = std::move(q.front());
            q.pop_front();
        }
        const bool wantFull = t.done != nullptr;
        if (wantFull != fullSync) {
            writer_.exec(wantFull ? "PRAGMA synchronous=FULL;" : "PRAGMA synchronous=NORMAL;");
            fullSync = wantFull;
        }
        const auto execStart = std::chrono::steady_clock::now();
        Status st = t.fn(writer_);
        {
            std::lock_guard lock(qmu_);
            walDirty_ = !t.done;  // a FULL commit flushes every earlier WAL frame
            maintenanceDue_ = true;
        }
        if (t.done) {
            lastCriticalWaitMs_ = std::chrono::duration<double, std::milli>(execStart - t.queuedAt).count();
            lastCriticalExecMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - execStart).count();
        }
        if (!st) {
            healthy_ = false;
            std::lock_guard lock(errMu_);
            lastError_ = st.error().message;
        } else if (t.done) {
            healthy_ = true;
        }
        if (t.done) t.done->set_value(st);
    }
}

Status Store::writeSync(std::function<Status(Database&)> fn) {
    auto p = std::make_shared<std::promise<Status>>();
    auto f = p->get_future();
    {
        std::lock_guard lock(qmu_);
        if (stopping_) return failStatus("store_closed", "Store is shutting down");
        queue_.push_back({std::move(fn), p, std::chrono::steady_clock::now()});
    }
    qcv_.notify_one();
    return f.get();
}

void Store::writeAsync(std::function<Status(Database&)> fn) {
    {
        std::lock_guard lock(qmu_);
        if (stopping_) return;
        background_.push_back({std::move(fn), nullptr, std::chrono::steady_clock::now()});
    }
    qcv_.notify_one();
}

// messages & signals

Result<bool> Store::admitRawMessage(const IncomingMessage& m) {
    const bool exists = read([&](Database& db) {
        auto q = db.prepare("SELECT 1 FROM raw_messages WHERE chat_id=? AND message_id=? AND edit_version=?");
        if (!q) return false;
        q.value().bind(1, m.chatId).bind(2, m.messageId).bind(3, m.editVersion);
        return q.value().step();
    });
    if (exists) return false;  // replay after restart/reconnect (in-session replays are caught in memory first)
    IncomingMessage copy = m;
    writeAsync([copy](Database& db) -> Status {
        auto q = db.prepare(
            "INSERT OR IGNORE INTO raw_messages(chat_id,message_id,edit_version,sender_id,reply_to,published_at,received_at,text,has_media,media_kind,historical)"
            " VALUES(?,?,?,?,?,?,?,?,?,?,?)");
        if (!q) return q.error();
        auto& s = q.value();
        s.bind(1, copy.chatId).bind(2, copy.messageId).bind(3, copy.editVersion).bind(4, copy.senderId).bind(5, copy.replyToMessageId).bind(6, copy.publishedAtMs);
        s.bind(7, copy.receivedAtMs).bind(8, copy.text).bind(9, copy.hasMedia ? 1 : 0).bind(10, copy.mediaKind).bind(11, copy.historical ? 1 : 0);
        return s.run();
    });
    return true;
}

Result<bool> Store::insertRawMessage(const IncomingMessage& m) {
    bool inserted = false;
    auto st = writeSync([&](Database& db) -> Status {
        auto q = db.prepare(
            "INSERT OR IGNORE INTO raw_messages(chat_id,message_id,edit_version,sender_id,reply_to,published_at,received_at,text,has_media,media_kind,historical)"
            " VALUES(?,?,?,?,?,?,?,?,?,?,?)");
        if (!q) return q.error();
        auto& s = q.value();
        s.bind(1, m.chatId).bind(2, m.messageId).bind(3, m.editVersion).bind(4, m.senderId).bind(5, m.replyToMessageId).bind(6, m.publishedAtMs).bind(7, m.receivedAtMs);
        s.bind(8, m.text).bind(9, m.hasMedia ? 1 : 0).bind(10, m.mediaKind).bind(11, m.historical ? 1 : 0);
        if (auto r = s.run(); !r) return r;
        inserted = db.changes() > 0;
        return okStatus();
    });
    if (!st) return st.error();
    return inserted;
}

Status Store::upsertSignal(const Signal& s, SignalState state, const json& decision, const json& timeline) {
    const std::string sj = toJson(s).dump();
    const std::string dj = decision.dump();
    const std::string tj = timeline.dump();
    const std::int64_t now = clock_.nowMs();
    writeAsync([=](Database& db) -> Status {
        auto q = db.prepare(
            "INSERT INTO signal_revisions(chat_id,message_id,revision,parser_id,parser_version,status,kind,state,symbol,side,signal_json,decision_json,timeline_json,created_at,updated_at)"
            " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"
            " ON CONFLICT(chat_id,message_id,revision) DO UPDATE SET state=excluded.state, decision_json=excluded.decision_json, timeline_json=excluded.timeline_json,"
            " signal_json=excluded.signal_json, status=excluded.status, updated_at=excluded.updated_at");
        if (!q) return q.error();
        auto& st = q.value();
        st.bind(1, s.source.chatId).bind(2, s.source.messageId).bind(3, s.revision).bind(4, s.parserId).bind(5, s.parserVersion).bind(6, toString(s.status)).bind(7, toString(s.kind));
        st.bind(8, toString(state)).bind(9, s.symbol).bind(10, s.side ? toString(*s.side) : "").bind(11, sj).bind(12, dj).bind(13, tj).bind(14, now).bind(15, now);
        return st.run();
    });
    return okStatus();  // not submission-critical: FIFO writer keeps ordering; failures mark the store unhealthy
}

Status Store::updateSignalState(const SourceKey& k, int revision, SignalState state, const json& decision, const json& timeline) {
    const std::string dj = decision.dump(), tj = timeline.dump();
    const std::int64_t now = clock_.nowMs();
    writeAsync([=](Database& db) -> Status {
        auto q = db.prepare("UPDATE signal_revisions SET state=?, decision_json=?, timeline_json=?, updated_at=? WHERE chat_id=? AND message_id=? AND revision=?");
        if (!q) return q.error();
        q.value().bind(1, toString(state)).bind(2, dj).bind(3, tj).bind(4, now).bind(5, k.chatId).bind(6, k.messageId).bind(7, revision);
        return q.value().run();
    });
    return okStatus();  // not submission-critical: FIFO writer keeps ordering; failures mark the store unhealthy
}

Status Store::updateSignalAi(const SourceKey& k, int revision, const json& ai) {
    const std::string aj = ai.dump();
    writeAsync([=](Database& db) -> Status {
        auto q = db.prepare("UPDATE signal_revisions SET ai_json=? WHERE chat_id=? AND message_id=? AND revision=?");
        if (!q) return q.error();
        q.value().bind(1, aj).bind(2, k.chatId).bind(3, k.messageId).bind(4, revision);
        return q.value().run();
    });
    return okStatus();
}

std::optional<SignalState> Store::signalState(const SourceKey& k, int revision) {
    return read([&](Database& db) -> std::optional<SignalState> {
        auto q = db.prepare("SELECT state FROM signal_revisions WHERE chat_id=? AND message_id=? AND revision=?");
        if (!q) return std::nullopt;
        q.value().bind(1, k.chatId).bind(2, k.messageId).bind(3, revision);
        if (!q.value().step()) return std::nullopt;
        const std::string s = q.value().text(0);
        for (int i = 0; i <= static_cast<int>(SignalState::Historical); ++i)
            if (s == toString(static_cast<SignalState>(i))) return static_cast<SignalState>(i);
        return std::nullopt;
    });
}

bool Store::hasSubmittedRevision(const SourceKey& k) {
    return read([&](Database& db) {
        auto q = db.prepare("SELECT 1 FROM execution_plans p JOIN commands c ON c.plan_id=p.plan_id WHERE p.chat_id=? AND p.message_id=? AND c.state<>'Prepared' LIMIT 1");
        if (!q) return true;  // fail safe: treat as submitted
        q.value().bind(1, k.chatId).bind(2, k.messageId);
        return q.value().step();
    });
}

// execution journal

Status Store::recordPlan(const ExecutionPlan& plan, const std::vector<Command>& commands) {
    const std::string pj = toJson(plan).dump();
    const std::int64_t now = clock_.nowMs();
    const std::string accountKey = plan.account.str();
    return writeSync([=, this](Database& db) -> Status {
        const auto bodyStart = std::chrono::steady_clock::now();
        Transaction tx(db);
        if (!tx.active()) return failStatus("db_tx", "Cannot begin journal transaction: " + db.lastError());
        auto p = db.prepare("INSERT INTO execution_plans(plan_id,chat_id,message_id,revision,account_key,config_version,plan_json,created_at) VALUES(?,?,?,?,?,?,?,?)");
        if (!p) return p.error();
        p.value().bind(1, plan.planId).bind(2, plan.source.chatId).bind(3, plan.source.messageId).bind(4, plan.revision).bind(5, accountKey).bind(6, plan.configVersion).bind(7, pj).bind(8, now);
        if (auto r = p.value().run(); !r) return r;
        for (auto& c : commands) {
            auto q = db.prepare(
                "INSERT INTO commands(command_id,plan_id,child_index,action,symbol,volume,state,deadline,payload_json,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?)");
            if (!q) return q.error();
            double volume = 0;
            for (auto& ch : plan.children)
                if (ch.index == c.childIndex) volume = ch.volume;
            q.value().bind(1, c.commandId).bind(2, plan.planId).bind(3, c.childIndex).bind(4, c.action).bind(5, plan.brokerSymbol).bind(6, volume).bind(7, toString(c.state));
            q.value().bind(8, c.deadlineMs).bind(9, c.payloadJson).bind(10, now).bind(11, now);
            if (auto r = q.value().run(); !r) return r;
            auto e = db.prepare("INSERT INTO command_events(command_id,from_state,to_state,detail_json,at) VALUES(?,NULL,'Prepared','{}',?)");
            if (!e) return e.error();
            e.value().bind(1, c.commandId).bind(2, now);
            if (auto r = e.value().run(); !r) return r;
            if (c.state == CommandState::Dispatching) {
                // First child: Prepared and Dispatching are journaled in ONE durable commit (single flush).
                auto e2 = db.prepare("INSERT INTO command_events(command_id,from_state,to_state,detail_json,at) VALUES(?,'Prepared','Dispatching','{}',?)");
                if (!e2) return e2.error();
                e2.value().bind(1, c.commandId).bind(2, now);
                if (auto r = e2.value().run(); !r) return r;
            }
            auto res = db.prepare("INSERT INTO reservations(command_id,account_key,volume,risk_money,created_at) VALUES(?,?,?,?,?)");
            if (!res) return res.error();
            const double share = plan.totalVolume > 0 ? volume / plan.totalVolume : 0;
            res.value().bind(1, c.commandId).bind(2, accountKey).bind(3, volume).bind(4, plan.plannedRiskMoney * share).bind(5, now);
            if (auto r = res.value().run(); !r) return r;
        }
        const auto flushStart = std::chrono::steady_clock::now();
        auto committed = tx.commit();
        lastPlanFlushMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - flushStart).count();
        lastPlanBodyMs_ = std::chrono::duration<double, std::milli>(flushStart - bodyStart).count();
        return committed;
    });
}

Status Store::recordCommand(const Command& c, const ExecutionPlan& plan) { return recordPlan(plan, {c}); }

Status Store::transition(const std::string& commandId, CommandState to, const std::string& detailJson) {
    const std::int64_t now = clock_.nowMs();
    return writeSync([=](Database& db) -> Status {
        Transaction tx(db);
        if (!tx.active()) return failStatus("db_tx", db.lastError());
        auto cur = db.prepare("SELECT state FROM commands WHERE command_id=?");
        if (!cur) return cur.error();
        cur.value().bind(1, commandId);
        if (!cur.value().step()) return failStatus("not_found", "Unknown command " + commandId);
        auto from = commandStateFromString(cur.value().text(0));
        if (!from) return failStatus("state_invalid", "Corrupt command state");
        if (!canTransition(*from, to)) return failStatus("transition_invalid", std::string("Illegal transition ") + toString(*from) + " -> " + toString(to));
        auto up = db.prepare("UPDATE commands SET state=?, resolved=?, updated_at=? WHERE command_id=?");
        if (!up) return up.error();
        up.value().bind(1, toString(to)).bind(2, isTerminal(to) ? 1 : 0).bind(3, now).bind(4, commandId);
        if (auto r = up.value().run(); !r) return r;
        auto ev = db.prepare("INSERT INTO command_events(command_id,from_state,to_state,detail_json,at) VALUES(?,?,?,?,?)");
        if (!ev) return ev.error();
        ev.value().bind(1, commandId).bind(2, toString(*from)).bind(3, toString(to)).bind(4, detailJson).bind(5, now);
        if (auto r = ev.value().run(); !r) return r;
        // Reservations are released only on rejection, cancellation or expiry here; fills keep them until
        // the position is reconciled as closed.
        if (to == CommandState::Rejected || to == CommandState::Canceled || to == CommandState::Expired) {
            auto rel = db.prepare("UPDATE reservations SET released_at=?, release_reason=? WHERE command_id=? AND released_at IS NULL");
            if (!rel) return rel.error();
            rel.value().bind(1, now).bind(2, toString(to)).bind(3, commandId);
            if (auto r = rel.value().run(); !r) return r;
        }
        return tx.commit();
    });
}

Status Store::setCommandBrokerIds(const std::string& commandId, const std::string& requestId, const std::string& orderTicket, double filledVolume) {
    const std::int64_t now = clock_.nowMs();
    return writeSync([=](Database& db) -> Status {
        auto q = db.prepare(
            "UPDATE commands SET request_id=COALESCE(NULLIF(?,''),request_id), order_ticket=COALESCE(NULLIF(?,''),order_ticket),"
            " filled_volume=MAX(filled_volume, ?), updated_at=? WHERE command_id=?");
        if (!q) return q.error();
        q.value().bind(1, requestId).bind(2, orderTicket).bind(3, filledVolume).bind(4, now).bind(5, commandId);
        return q.value().run();
    });
}

std::vector<Command> Store::unresolvedCommands() {
    std::vector<Command> out;
    for (auto& u : unresolvedDetailed()) out.push_back(u.command);
    return out;
}

std::vector<UnresolvedCommand> Store::unresolvedDetailed() {
    return read([&](Database& db) {
        std::vector<UnresolvedCommand> out;
        auto q = db.prepare(
            "SELECT c.command_id,c.plan_id,c.child_index,c.action,c.deadline,c.state,c.payload_json,c.symbol,c.volume,p.account_key,COALESCE(c.request_id,''),COALESCE(c.order_ticket,'')"
            " FROM commands c JOIN execution_plans p ON p.plan_id=c.plan_id WHERE c.resolved=0 ORDER BY c.created_at");
        if (!q) return out;
        auto& s = q.value();
        while (s.step()) {
            UnresolvedCommand u;
            u.command.commandId = s.text(0);
            u.command.planId = s.text(1);
            u.command.childIndex = static_cast<int>(s.i64(2));
            u.command.action = s.text(3);
            u.command.deadlineMs = s.i64(4);
            u.command.state = commandStateFromString(s.text(5)).value_or(CommandState::Unknown);
            u.command.payloadJson = s.text(6);
            u.symbol = s.text(7);
            u.volume = s.dbl(8);
            u.accountKey = s.text(9);
            u.requestId = s.text(10);
            u.orderTicket = s.text(11);
            out.push_back(std::move(u));
        }
        return out;
    });
}

std::optional<CommandState> Store::commandState(const std::string& commandId) {
    return read([&](Database& db) -> std::optional<CommandState> {
        auto q = db.prepare("SELECT state FROM commands WHERE command_id=?");
        if (!q) return std::nullopt;
        q.value().bind(1, commandId);
        if (!q.value().step()) return std::nullopt;
        return commandStateFromString(q.value().text(0));
    });
}

std::optional<std::string> Store::commandIdForTicket(const std::string& ticket) {
    return read([&](Database& db) -> std::optional<std::string> {
        auto q = db.prepare(
            "SELECT command_id FROM commands WHERE order_ticket=? UNION SELECT command_id FROM positions WHERE position_id=? AND command_id IS NOT NULL LIMIT 1");
        if (!q) return std::nullopt;
        q.value().bind(1, ticket).bind(2, ticket);
        if (!q.value().step()) return std::nullopt;
        return q.value().text(0);
    });
}

std::optional<std::int64_t> Store::chatIdForCommand(const std::string& commandId) {
    return read([&](Database& db) -> std::optional<std::int64_t> {
        auto q = db.prepare("SELECT p.chat_id FROM commands c JOIN execution_plans p ON p.plan_id=c.plan_id WHERE c.command_id=?");
        if (!q) return std::nullopt;
        q.value().bind(1, commandId);
        if (!q.value().step()) return std::nullopt;
        return q.value().i64(0);
    });
}

// broker facts

Result<bool> Store::upsertDeal(const Deal& d, const std::string& accountKey) {
    bool inserted = false;
    auto st = writeSync([&](Database& db) -> Status {
        auto q = db.prepare(
            "INSERT OR IGNORE INTO broker_deals(ticket,order_ticket,position_id,command_id,chat_id,symbol,deal_type,entry,volume,price,profit,commission,swap,fee,magic,time,account_key)"
            " VALUES(?,?,?,NULLIF(?,''),?,?,?,?,?,?,?,?,?,?,?,?,?)");
        if (!q) return q.error();
        auto& s = q.value();
        s.bind(1, std::to_string(d.ticket)).bind(2, std::to_string(d.order)).bind(3, std::to_string(d.positionId)).bind(4, d.commandId).bind(5, d.sourceChatId).bind(6, d.symbol);
        s.bind(7, d.type).bind(8, d.entry).bind(9, d.volume).bind(10, d.price).bind(11, d.profit).bind(12, d.commission).bind(13, d.swap).bind(14, d.fee).bind(15, d.magic).bind(16, d.timeMs);
        s.bind(17, accountKey);
        if (auto r = s.run(); !r) return r;
        inserted = db.changes() > 0;
        if (!inserted && !d.commandId.empty()) {
            auto u = db.prepare("UPDATE broker_deals SET command_id=?, chat_id=? WHERE ticket=? AND command_id IS NULL");
            if (!u) return u.error();
            u.value().bind(1, d.commandId).bind(2, d.sourceChatId).bind(3, std::to_string(d.ticket));
            if (auto r = u.value().run(); !r) return r;
        }
        // A closing deal for a copier position releases that command's reservation (reconciled completion).
        if (inserted && (d.entry == 1 || d.entry == 3) && !d.commandId.empty()) {
            auto rel = db.prepare("UPDATE reservations SET released_at=?, release_reason='position closed' WHERE command_id=? AND released_at IS NULL");
            if (!rel) return rel.error();
            rel.value().bind(1, d.timeMs).bind(2, d.commandId);
            if (auto r = rel.value().run(); !r) return r;
        }
        return okStatus();
    });
    if (!st) return st.error();
    return inserted;
}

std::vector<Deal> Store::loadDeals(std::int64_t fromMs) {
    return read([&](Database& db) {
        std::vector<Deal> out;
        auto q = db.prepare(
            "SELECT ticket,order_ticket,position_id,COALESCE(command_id,''),COALESCE(chat_id,0),symbol,deal_type,entry,volume,price,profit,commission,swap,fee,magic,time"
            " FROM broker_deals WHERE time>=? ORDER BY time");
        if (!q) return out;
        auto& s = q.value();
        s.bind(1, fromMs);
        while (s.step()) {
            Deal d;
            d.ticket = std::stoull(s.text(0));
            d.order = std::stoull(s.text(1).empty() ? "0" : s.text(1));
            d.positionId = std::stoull(s.text(2).empty() ? "0" : s.text(2));
            d.commandId = s.text(3);
            d.sourceChatId = s.i64(4);
            d.symbol = s.text(5);
            d.type = static_cast<int>(s.i64(6));
            d.entry = static_cast<int>(s.i64(7));
            d.volume = s.dbl(8);
            d.price = s.dbl(9);
            d.profit = s.dbl(10);
            d.commission = s.dbl(11);
            d.swap = s.dbl(12);
            d.fee = s.dbl(13);
            d.magic = s.i64(14);
            d.timeMs = s.i64(15);
            out.push_back(std::move(d));
        }
        return out;
    });
}

Status Store::upsertPosition(const std::string& positionId, const std::string& commandId, const std::string& symbol, const std::string& side, double volume, double openPrice,
                             double sl, double tp, double profit, bool closed) {
    const std::int64_t now = clock_.nowMs();
    writeAsync([=](Database& db) -> Status {
        auto q = db.prepare(
            "INSERT INTO positions(position_id,command_id,symbol,side,volume,open_price,sl,tp,profit,closed,updated_at) VALUES(?,NULLIF(?,''),?,?,?,?,?,?,?,?,?)"
            " ON CONFLICT(position_id) DO UPDATE SET volume=excluded.volume, sl=excluded.sl, tp=excluded.tp, profit=excluded.profit, closed=excluded.closed,"
            " command_id=COALESCE(positions.command_id, excluded.command_id), updated_at=excluded.updated_at");
        if (!q) return q.error();
        q.value().bind(1, positionId).bind(2, commandId).bind(3, symbol).bind(4, side).bind(5, volume).bind(6, openPrice).bind(7, sl).bind(8, tp).bind(9, profit).bind(10, closed ? 1 : 0).bind(11, now);
        return q.value().run();
    });
    return okStatus();
}

Status Store::saveProfitSnapshot(std::int64_t timeMs, const std::string& accountKey, double balance, double equity, double floating) {
    writeAsync([=](Database& db) -> Status {
        auto q = db.prepare("INSERT OR REPLACE INTO profit_snapshots(time,account_key,balance,equity,floating) VALUES(?,?,?,?,?)");
        if (!q) return q.error();
        q.value().bind(1, timeMs).bind(2, accountKey).bind(3, balance).bind(4, equity).bind(5, floating);
        return q.value().run();
    });
    return okStatus();
}

json Store::equityCurve(std::int64_t fromMs, std::size_t maxPoints) {
    return read([&](Database& db) {
        std::vector<CurvePoint> eq, bal;
        auto q = db.prepare("SELECT time,equity,balance FROM profit_snapshots WHERE time>=? ORDER BY time");
        if (q) {
            q.value().bind(1, fromMs);
            while (q.value().step()) {
                eq.push_back({q.value().i64(0), q.value().dbl(1)});
                bal.push_back({q.value().i64(0), q.value().dbl(2)});
            }
        }
        json e = json::array(), b = json::array();
        for (auto& p : downsample(eq, maxPoints)) e.push_back({p.timeMs, p.value});
        for (auto& p : downsample(bal, maxPoints)) b.push_back({p.timeMs, p.value});
        return json{{"equity", e}, {"balance", b}};
    });
}

// configuration

Status Store::putSetting(const std::string& key, const std::string& value) {
    const std::int64_t now = clock_.nowMs();
    return writeSync([=](Database& db) -> Status {
        auto q = db.prepare("INSERT INTO settings(key,value,updated_at) VALUES(?,?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value, updated_at=excluded.updated_at");
        if (!q) return q.error();
        q.value().bind(1, key).bind(2, value).bind(3, now);
        return q.value().run();
    });
}

std::optional<std::string> Store::getSetting(const std::string& key) {
    return read([&](Database& db) -> std::optional<std::string> {
        auto q = db.prepare("SELECT value FROM settings WHERE key=?");
        if (!q) return std::nullopt;
        q.value().bind(1, key);
        if (!q.value().step()) return std::nullopt;
        return q.value().text(0);
    });
}

Status Store::savePolicy(const json& policy, std::int64_t configVersion) {
    const std::string pj = policy.dump();
    const std::int64_t now = clock_.nowMs();
    return writeSync([=](Database& db) -> Status {
        auto q = db.prepare("INSERT OR REPLACE INTO policies(config_version,policy_json,created_at) VALUES(?,?,?)");
        if (!q) return q.error();
        q.value().bind(1, configVersion).bind(2, pj).bind(3, now);
        return q.value().run();
    });
}

Status Store::replaceSources(const json& sources) {
    const std::int64_t now = clock_.nowMs();
    const std::string dump = sources.dump();
    return writeSync([=](Database& db) -> Status {
        json list = json::parse(dump);
        Transaction tx(db);
        if (auto r = db.exec("DELETE FROM sources;"); !r) return r;
        for (auto& b : list) {
            auto q = db.prepare("INSERT INTO sources(chat_id,title,chat_type,binding_json,updated_at) VALUES(?,?,?,?,?)");
            if (!q) return q.error();
            q.value().bind(1, jsonInt64(b.value("chatId", json("0")))).bind(2, b.value("title", "")).bind(3, b.value("chatType", "")).bind(4, b.dump()).bind(5, now);
            if (auto r = q.value().run(); !r) return r;
        }
        return tx.commit();
    });
}

std::optional<json> Store::latestPolicy() {
    return read([&](Database& db) -> std::optional<json> {
        auto q = db.prepare("SELECT policy_json FROM policies ORDER BY config_version DESC LIMIT 1");
        if (!q || !q.value().step()) return std::nullopt;
        json j = parseOr(q.value().text(0));
        if (j.is_null()) return std::nullopt;
        return j;
    });
}

Status Store::saveParserVersion(const ParserVersionInfo& v) {
    const std::string tj = v.templateJson.is_null() ? "" : v.templateJson.dump();
    return writeSync([=](Database& db) -> Status {
        Transaction tx(db);
        if (v.active) {
            auto clr = db.prepare("UPDATE parser_versions SET active=0 WHERE parser_id=?");
            if (!clr) return clr.error();
            clr.value().bind(1, v.parserId);
            if (auto r = clr.value().run(); !r) return r;
        }
        auto q = db.prepare(
            "INSERT INTO parser_versions(parser_id,version,origin,template_json,created_at,activated_at,active) VALUES(?,?,?,NULLIF(?,''),?,?,?)"
            " ON CONFLICT(parser_id,version) DO UPDATE SET activated_at=excluded.activated_at, active=excluded.active");
        if (!q) return q.error();
        q.value().bind(1, v.parserId).bind(2, v.version).bind(3, v.origin).bind(4, tj).bind(5, v.createdAtMs).bind(6, v.activatedAtMs).bind(7, v.active ? 1 : 0);
        if (auto r = q.value().run(); !r) return r;
        return tx.commit();
    });
}

std::vector<ParserVersionInfo> Store::loadParserVersions() {
    return read([&](Database& db) {
        std::vector<ParserVersionInfo> out;
        auto q = db.prepare("SELECT parser_id,version,origin,COALESCE(template_json,''),created_at,COALESCE(activated_at,0),active FROM parser_versions ORDER BY created_at");
        if (!q) return out;
        while (q.value().step()) {
            ParserVersionInfo v;
            v.parserId = q.value().text(0);
            v.version = q.value().text(1);
            v.origin = q.value().text(2);
            v.templateJson = parseOr(q.value().text(3));
            v.createdAtMs = q.value().i64(4);
            v.activatedAtMs = q.value().i64(5);
            v.active = q.value().i64(6) != 0;
            out.push_back(std::move(v));
        }
        return out;
    });
}

Status Store::saveFixtures(const std::string& parserId, const std::vector<Fixture>& fixtures) {
    std::vector<std::pair<std::string, std::string>> rows;
    for (auto& f : fixtures) rows.push_back({f.id, f.toJson().dump()});
    return writeSync([=](Database& db) -> Status {
        Transaction tx(db);
        auto del = db.prepare("DELETE FROM parser_fixtures WHERE parser_id=?");
        if (!del) return del.error();
        del.value().bind(1, parserId);
        if (auto r = del.value().run(); !r) return r;
        for (auto& [id, js] : rows) {
            auto q = db.prepare("INSERT INTO parser_fixtures(parser_id,fixture_id,fixture_json) VALUES(?,?,?)");
            if (!q) return q.error();
            q.value().bind(1, parserId).bind(2, id).bind(3, js);
            if (auto r = q.value().run(); !r) return r;
        }
        return tx.commit();
    });
}

std::vector<Fixture> Store::loadFixtures(const std::string& parserId) {
    return read([&](Database& db) {
        std::vector<Fixture> out;
        auto q = db.prepare("SELECT fixture_json FROM parser_fixtures WHERE parser_id=? ORDER BY fixture_id");
        if (!q) return out;
        q.value().bind(1, parserId);
        while (q.value().step()) {
            json j = parseOr(q.value().text(0));
            if (j.is_object()) out.push_back(Fixture::fromJson(j));
        }
        return out;
    });
}

Status Store::setWatermark(const std::string& scope, std::int64_t value) {
    const std::int64_t now = clock_.nowMs();
    return writeSync([=](Database& db) -> Status {
        auto q = db.prepare(
            "INSERT INTO session_watermarks(scope,value,updated_at) VALUES(?,?,?) ON CONFLICT(scope) DO UPDATE SET value=MAX(value, excluded.value), updated_at=excluded.updated_at");
        if (!q) return q.error();
        q.value().bind(1, scope).bind(2, value).bind(3, now);
        return q.value().run();
    });
}

std::optional<std::int64_t> Store::watermark(const std::string& scope) {
    return read([&](Database& db) -> std::optional<std::int64_t> {
        auto q = db.prepare("SELECT value FROM session_watermarks WHERE scope=?");
        if (!q) return std::nullopt;
        q.value().bind(1, scope);
        if (!q.value().step()) return std::nullopt;
        return q.value().i64(0);
    });
}

Status Store::saveAccountBinding(const AccountKey& key) {
    const std::int64_t now = clock_.nowMs();
    return writeSync([=](Database& db) -> Status {
        Transaction tx(db);
        if (auto r = db.exec("UPDATE account_bindings SET active=0;"); !r) return r;
        auto q = db.prepare(
            "INSERT INTO account_bindings(account_key,terminal_id,login,server,approved_at,active) VALUES(?,?,?,?,?,1)"
            " ON CONFLICT(account_key) DO UPDATE SET active=1, approved_at=excluded.approved_at");
        if (!q) return q.error();
        q.value().bind(1, key.str()).bind(2, key.terminalId).bind(3, key.login).bind(4, key.server).bind(5, now);
        if (auto r = q.value().run(); !r) return r;
        return tx.commit();
    });
}

std::optional<AccountKey> Store::activeAccountBinding() {
    return read([&](Database& db) -> std::optional<AccountKey> {
        auto q = db.prepare("SELECT terminal_id,login,server FROM account_bindings WHERE active=1 LIMIT 1");
        if (!q || !q.value().step()) return std::nullopt;
        return AccountKey{q.value().text(0), q.value().i64(1), q.value().text(2)};
    });
}

Status Store::clearAccountBindings() {
    return writeSync([](Database& db) { return db.exec("UPDATE account_bindings SET active=0;"); });
}

// updates & AI

Status Store::addUpdateHistory(const std::string& releaseId, const std::string& component, const std::string& from, const std::string& to, const std::string& state,
                               const json& notes, bool restartRequired) {
    const std::string nj = notes.dump();
    const std::int64_t now = clock_.nowMs();
    return writeSync([=](Database& db) -> Status {
        auto q = db.prepare("INSERT INTO update_history(release_id,component,from_version,to_version,state,notes_json,restart_required,at) VALUES(?,?,?,?,?,?,?,?)");
        if (!q) return q.error();
        q.value().bind(1, releaseId).bind(2, component).bind(3, from).bind(4, to).bind(5, state).bind(6, nj).bind(7, restartRequired ? 1 : 0).bind(8, now);
        return q.value().run();
    });
}

json Store::updateHistory(int limit) {
    return read([&](Database& db) {
        json arr = json::array();
        auto q = db.prepare("SELECT id,release_id,component,from_version,to_version,state,notes_json,restart_required,at,notes_shown FROM update_history ORDER BY id DESC LIMIT ?");
        if (!q) return arr;
        q.value().bind(1, limit);
        while (q.value().step()) {
            auto& s = q.value();
            arr.push_back({{"id", s.i64(0)}, {"releaseId", s.text(1)}, {"component", s.text(2)}, {"from", s.text(3)}, {"to", s.text(4)}, {"state", s.text(5)},
                           {"notes", parseOr(s.text(6), json::object())}, {"restartRequired", s.i64(7) != 0}, {"at", s.i64(8)}, {"notesShown", s.i64(9) != 0}});
        }
        return arr;
    });
}

Status Store::markUpdateNotesShown(std::int64_t id) {
    return writeSync([=](Database& db) -> Status {
        auto q = db.prepare("UPDATE update_history SET notes_shown=1 WHERE id=?");
        if (!q) return q.error();
        q.value().bind(1, id);
        return q.value().run();
    });
}

Status Store::saveAiAssessment(const SourceKey& k, int revision, const json& a) {
    const std::string aj = a.dump();
    const std::int64_t now = clock_.nowMs();
    writeAsync([=](Database& db) -> Status {
        auto q = db.prepare("INSERT INTO ai_assessments(chat_id,message_id,revision,assessment_json,created_at) VALUES(?,?,?,?,?)");
        if (!q) return q.error();
        q.value().bind(1, k.chatId).bind(2, k.messageId).bind(3, revision).bind(4, aj).bind(5, now);
        return q.value().run();
    });
    return okStatus();
}

// UI queries

namespace {

std::string historyWhere(const HistoryFilter& f) {
    std::string w = " WHERE 1=1";
    if (!f.text.empty()) w += " AND (s.signal_json LIKE ?1 OR s.symbol LIKE ?1 OR s.state LIKE ?1 OR r.text LIKE ?1)";
    if (!f.state.empty()) w += " AND s.state = ?2";
    if (f.chatId != 0) w += " AND s.chat_id = ?3";
    if (f.fromMs != 0) w += " AND s.created_at >= ?4";
    if (f.toMs != 0) w += " AND s.created_at <= ?5";
    return w;
}

void bindHistory(Statement& s, const HistoryFilter& f) {
    if (!f.text.empty()) s.bind(1, "%" + f.text + "%");
    if (!f.state.empty()) s.bind(2, f.state);
    if (f.chatId != 0) s.bind(3, f.chatId);
    if (f.fromMs != 0) s.bind(4, f.fromMs);
    if (f.toMs != 0) s.bind(5, f.toMs);
}

const char* kHistoryFrom =
    " FROM signal_revisions s LEFT JOIN raw_messages r ON r.chat_id=s.chat_id AND r.message_id=s.message_id AND r.edit_version=s.revision"
    " LEFT JOIN sources src ON src.chat_id=s.chat_id";

}  // namespace

std::int64_t Store::historyCount(const HistoryFilter& f) {
    return read([&](Database& db) -> std::int64_t {
        auto q = db.prepare(std::string("SELECT COUNT(*)") + kHistoryFrom + historyWhere(f));
        if (!q) return 0;
        bindHistory(q.value(), f);
        return q.value().step() ? q.value().i64(0) : 0;
    });
}

json Store::historyPage(const HistoryFilter& f, std::int64_t offset, int limit) {
    return read([&](Database& db) {
        json arr = json::array();
        auto q = db.prepare(std::string("SELECT s.chat_id,s.message_id,s.revision,s.state,s.status,s.kind,s.symbol,s.side,s.created_at,COALESCE(src.title,''),"
                                        "COALESCE(substr(r.text,1,160),''),s.parser_id,s.parser_version,COALESCE(s.decision_json,'')") +
                            kHistoryFrom + historyWhere(f) + " ORDER BY s.created_at DESC, s.id DESC LIMIT ?6 OFFSET ?7");
        if (!q) return arr;
        auto& s = q.value();
        bindHistory(s, f);
        s.bind(6, limit).bind(7, offset);
        while (s.step()) {
            json d = parseOr(s.text(13), json::object());
            arr.push_back({{"chatId", std::to_string(s.i64(0))}, {"messageId", std::to_string(s.i64(1))}, {"revision", s.i64(2)}, {"state", s.text(3)},
                           {"status", s.text(4)}, {"kind", s.text(5)}, {"symbol", s.text(6)}, {"side", s.text(7)}, {"at", s.i64(8)}, {"source", s.text(9)},
                           {"preview", s.text(10)}, {"parser", s.text(11) + "@" + s.text(12)}, {"reason", d.is_object() ? d.value("reason", "") : ""}});
        }
        return arr;
    });
}

json Store::signalDetail(std::int64_t chatId, std::int64_t messageId, int revision) {
    return read([&](Database& db) {
        json out = json::object();
        auto q = db.prepare(
            "SELECT s.signal_json,COALESCE(s.decision_json,''),COALESCE(s.ai_json,''),COALESCE(s.timeline_json,''),s.state,COALESCE(r.text,''),COALESCE(r.published_at,0),"
            "COALESCE(r.received_at,0),COALESCE(r.historical,0) FROM signal_revisions s LEFT JOIN raw_messages r ON r.chat_id=s.chat_id AND"
            " r.message_id=s.message_id AND r.edit_version=s.revision WHERE s.chat_id=? AND s.message_id=? AND s.revision=?");
        if (!q) return out;
        q.value().bind(1, chatId).bind(2, messageId).bind(3, revision);
        if (!q.value().step()) return out;
        auto& s = q.value();
        out["signal"] = parseOr(s.text(0), json::object());
        out["decision"] = parseOr(s.text(1), json::object());
        out["ai"] = parseOr(s.text(2), nullptr);
        out["timeline"] = parseOr(s.text(3), json::array());
        out["state"] = s.text(4);
        out["rawText"] = s.text(5);
        out["publishedAtMs"] = s.i64(6);
        out["receivedAtMs"] = s.i64(7);
        out["historical"] = s.i64(8) != 0;
        json cmds = json::array();
        auto c = db.prepare(
            "SELECT c.command_id,c.child_index,c.action,c.symbol,c.volume,c.state,COALESCE(c.request_id,''),COALESCE(c.order_ticket,''),c.filled_volume,c.updated_at"
            " FROM commands c JOIN execution_plans p ON p.plan_id=c.plan_id WHERE p.chat_id=? AND p.message_id=? ORDER BY c.child_index");
        if (c) {
            c.value().bind(1, chatId).bind(2, messageId);
            while (c.value().step()) {
                auto& r = c.value();
                json events = json::array();
                auto e = db.prepare("SELECT COALESCE(from_state,''),to_state,COALESCE(detail_json,''),at FROM command_events WHERE command_id=? ORDER BY id");
                if (e) {
                    e.value().bind(1, r.text(0));
                    while (e.value().step())
                        events.push_back({{"from", e.value().text(0)}, {"to", e.value().text(1)}, {"detail", parseOr(e.value().text(2), json::object())}, {"at", e.value().i64(3)}});
                }
                cmds.push_back({{"commandId", r.text(0)}, {"child", r.i64(1)}, {"action", r.text(2)}, {"symbol", r.text(3)}, {"volume", r.dbl(4)}, {"state", r.text(5)},
                                {"requestId", r.text(6)}, {"orderTicket", r.text(7)}, {"filledVolume", r.dbl(8)}, {"updatedAt", r.i64(9)}, {"events", events}});
            }
        }
        out["commands"] = cmds;
        return out;
    });
}

json Store::recentFeed(int limit) {
    HistoryFilter f;
    return historyPage(f, 0, limit);
}

json Store::stats() {
    return read([&](Database& db) {
        json j = json::object();
        auto count = [&](const char* sql) -> std::int64_t {
            auto q = db.prepare(sql);
            return (q && q.value().step()) ? q.value().i64(0) : 0;
        };
        j["signals"] = count("SELECT COUNT(*) FROM signal_revisions");
        j["rawMessages"] = count("SELECT COUNT(*) FROM raw_messages");
        j["commands"] = count("SELECT COUNT(*) FROM commands");
        j["unresolvedCommands"] = count("SELECT COUNT(*) FROM commands WHERE resolved=0");
        j["deals"] = count("SELECT COUNT(*) FROM broker_deals");
        j["pendingReview"] = count("SELECT COUNT(*) FROM signal_revisions WHERE state='Review'");
        j["waiting"] = count("SELECT COUNT(*) FROM signal_revisions WHERE state='Waiting'");
        j["openReservations"] = count("SELECT COUNT(*) FROM reservations WHERE released_at IS NULL");
        return j;
    });
}

// maintenance

Result<json> Store::purgeRetention(int rawTextDays, int metadataDays) {
    const std::int64_t now = clock_.nowMs();
    const std::int64_t rawCut = now - static_cast<std::int64_t>(rawTextDays) * 86400000LL;
    const std::int64_t metaCut = now - static_cast<std::int64_t>(metadataDays) * 86400000LL;
    json result;
    auto st = writeSync([&](Database& db) -> Status {
        Transaction tx(db);
        // Raw text of messages linked to unresolved commands is kept as reconciliation evidence.
        auto a = db.prepare(
            "UPDATE raw_messages SET text=NULL WHERE received_at < ? AND text IS NOT NULL AND NOT EXISTS (SELECT 1 FROM execution_plans p JOIN commands c ON c.plan_id=p.plan_id"
            " WHERE p.chat_id=raw_messages.chat_id AND p.message_id=raw_messages.message_id AND c.resolved=0)");
        if (!a) return a.error();
        a.value().bind(1, rawCut);
        if (auto r = a.value().run(); !r) return r;
        result["rawTextCleared"] = db.changes();
        auto b = db.prepare(
            "DELETE FROM signal_revisions WHERE updated_at < ? AND NOT EXISTS (SELECT 1 FROM execution_plans p JOIN commands c ON c.plan_id=p.plan_id"
            " WHERE p.chat_id=signal_revisions.chat_id AND p.message_id=signal_revisions.message_id AND c.resolved=0)");
        if (!b) return b.error();
        b.value().bind(1, metaCut);
        if (auto r = b.value().run(); !r) return r;
        result["signalsDeleted"] = db.changes();
        auto c = db.prepare("DELETE FROM command_events WHERE at < ? AND command_id IN (SELECT command_id FROM commands WHERE resolved=1)");
        if (!c) return c.error();
        c.value().bind(1, metaCut);
        if (auto r = c.value().run(); !r) return r;
        result["commandEventsDeleted"] = db.changes();
        return tx.commit();
    });
    if (!st) return st.error();
    return result;
}

Status Store::eraseHistory() {
    return writeSync([](Database& db) -> Status {
        auto q = db.prepare("SELECT COUNT(*) FROM commands WHERE resolved=0");
        if (!q) return q.error();
        if (q.value().step() && q.value().i64(0) > 0)
            return failStatus("unresolved", "Local history cannot be erased while unresolved execution journals are needed to reconcile live exposure");
        Transaction tx(db);
        for (const char* sql : {"DELETE FROM command_events;", "DELETE FROM reservations;", "DELETE FROM commands;", "DELETE FROM execution_plans;", "DELETE FROM signal_revisions;",
                                "DELETE FROM raw_messages;", "DELETE FROM ai_assessments;", "DELETE FROM broker_deals;", "DELETE FROM broker_orders;", "DELETE FROM positions;",
                                "DELETE FROM profit_snapshots;"}) {
            if (auto r = db.exec(sql); !r) return r;
        }
        return tx.commit();
    });
}

Result<std::string> Store::integrityCheck() {
    return read([&](Database& db) -> Result<std::string> {
        auto q = db.prepare("PRAGMA integrity_check");
        if (!q) return q.error();
        std::string out;
        while (q.value().step()) out += q.value().text(0);
        auto fk = db.prepare("PRAGMA foreign_key_check");
        if (fk && fk.value().step()) out += " foreign-key violations present";
        return out;
    });
}

}  // namespace sr::db
