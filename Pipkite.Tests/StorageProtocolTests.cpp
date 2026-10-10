#include <windows.h>

#include <filesystem>
#include <thread>

#include "Pipkite.Core/Clock.h"
#include "Pipkite.Core/JsonModels.h"
#include "Pipkite.Core/Uuid.h"
#include "Pipkite.Protocol/Crypto.h"
#include "Pipkite.Protocol/Envelope.h"
#include "Pipkite.Protocol/Framing.h"
#include "Pipkite.Protocol/NamedPipe.h"
#include "Pipkite.Protocol/Platform.h"
#include "Pipkite.Storage/Backup.h"
#include "Pipkite.Storage/Store.h"
#include "TestFramework.h"

using namespace sr;
namespace fs = std::filesystem;

namespace {

std::wstring tempDir(const wchar_t* name) {
    auto p = fs::temp_directory_path() / (std::wstring(L"PipkiteTests-") + name + L"-" + std::to_wstring(GetCurrentProcessId()));
    fs::remove_all(p);
    fs::create_directories(p);
    return p.wstring();
}

std::unique_ptr<db::Store> openStore(const std::wstring& dir, const IClock& clock) {
    auto r = db::Store::open(platform::narrow(dir + L"\\test.sqlite"), clock);
    if (!r) throw srtest::Failure{"store open failed: " + r.error().message};
    return std::move(r.value());
}

ExecutionPlan samplePlan(const std::string& planId) {
    ExecutionPlan p;
    p.planId = planId;
    p.configVersion = 3;
    p.account = {"T1", 555, "Demo"};
    p.source = {-100, 7};
    p.brokerSymbol = "XAUUSD";
    p.totalVolume = 0.02;
    p.plannedRiskMoney = 20;
    p.children = {{0, 0.01, 4305.0, 1}, {1, 0.01, 4310.0, 2}};
    return p;
}

}  // namespace

TEST(store_raw_message_dedup_by_revision) {
    FakeClock clock;
    auto dir = tempDir(L"raw");
    {
        auto store = openStore(dir, clock);
        IncomingMessage m;
        m.chatId = -1001;
        m.messageId = 5;
        m.text = "GOLD BUY";
        CHECK(store->insertRawMessage(m).value());
        CHECK(!store->insertRawMessage(m).value());  // replay after reconnect
        m.editVersion = 1700000000;
        CHECK(store->insertRawMessage(m).value());
    }
    fs::remove_all(dir);
}

TEST(store_journal_durable_transitions_and_unresolved) {
    FakeClock clock;
    auto dir = tempDir(L"journal");
    {
        auto store = openStore(dir, clock);
        auto plan = samplePlan("plan-1");
        std::vector<Command> cmds;
        for (auto& c : plan.children) {
            Command cmd;
            cmd.commandId = newUuid();
            cmd.planId = plan.planId;
            cmd.childIndex = c.index;
            cmd.action = "BUY";
            cmd.deadlineMs = clock.nowMs() + 1000;
            cmd.payloadJson = "{}";
            cmds.push_back(cmd);
        }
        CHECK(store->recordPlan(plan, cmds).ok());
        CHECK(!store->recordPlan(plan, cmds).ok());  // unique constraints on plan/command ids
        CHECK_EQ(store->unresolvedCommands().size(), std::size_t(2));
        CHECK(store->transition(cmds[0].commandId, CommandState::Dispatching, "{}").ok());
        CHECK(!store->transition(cmds[0].commandId, CommandState::Prepared, "{}").ok());  // illegal
        CHECK(store->transition(cmds[0].commandId, CommandState::Unknown, "{\"reason\":\"timeout\"}").ok());
        CHECK(!store->transition(cmds[0].commandId, CommandState::Dispatching, "{}").ok());  // never blindly retried
        CHECK(store->transition(cmds[0].commandId, CommandState::Filled, "{}").ok());
        CHECK(store->transition(cmds[1].commandId, CommandState::Rejected, "{}").ok());
        CHECK_EQ(store->unresolvedCommands().size(), std::size_t(0));
        CHECK(store->healthy());
        auto stats = store->stats();
        CHECK_EQ(stats["openReservations"].get<std::int64_t>(), 1);  // rejected released, filled kept
    }
    // Reopen: state is durable.
    {
        auto store = openStore(dir, clock);
        CHECK_EQ(store->stats()["commands"].get<std::int64_t>(), 2);
    }
    fs::remove_all(dir);
}

TEST(store_deals_idempotent_and_history_virtual_paging) {
    FakeClock clock;
    auto dir = tempDir(L"hist");
    {
        auto store = openStore(dir, clock);
        Deal d;
        d.ticket = 99;
        d.type = 1;
        d.entry = 1;
        d.profit = 12.5;
        d.symbol = "XAUUSD";
        CHECK(store->upsertDeal(d, "k").value());
        CHECK(!store->upsertDeal(d, "k").value());
        CHECK_EQ(store->loadDeals().size(), std::size_t(1));
        // 100,000 history rows in one transaction; paging must stay fast.
        auto st = store->writeSync([&](db::Database& dbh) -> Status {
            db::Transaction tx(dbh);
            auto q = dbh.prepare("INSERT INTO signal_revisions(chat_id,message_id,revision,state,status,kind,symbol,side,signal_json,created_at,updated_at) VALUES(?,?,0,'Rejected','Valid','TradeSetup','XAUUSD','BUY','{}',?,?)");
            if (!q) return q.error();
            for (int i = 0; i < 100000; ++i) {
                q.value().reset();
                q.value().bind(1, static_cast<std::int64_t>(-100)).bind(2, static_cast<std::int64_t>(i)).bind(3, static_cast<std::int64_t>(i)).bind(4, static_cast<std::int64_t>(i));
                if (auto r = q.value().run(); !r) return r;
            }
            return tx.commit();
        });
        CHECK(st.ok());
        db::HistoryFilter f;
        CHECK_EQ(store->historyCount(f), 100000);
        auto t0 = std::chrono::steady_clock::now();
        auto page = store->historyPage(f, 50000, 100);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK_EQ(page.size(), std::size_t(100));
        CHECK(ms < 1500);
    }
    fs::remove_all(dir);
}

TEST(backup_roundtrip_plain_and_encrypted_with_tamper_detection) {
    FakeClock clock;
    auto dir = tempDir(L"backup");
    SetEnvironmentVariableW(L"PIPKITE_DATA_DIR", dir.c_str());
    {
        auto store = openStore(dir, clock);
        IncomingMessage m;
        m.chatId = -5;
        m.messageId = 1;
        m.text = "hello";
        store->insertRawMessage(m);
        store->saveAccountBinding({"T", 1, "S"});
        nlohmann::json cfg = {{"schemaVersion", 1}};
        // Export while writes continue on another thread (WAL active).
        std::atomic<bool> stop{false};
        std::thread writer([&] {
            int i = 100;
            while (!stop) {
                IncomingMessage x;
                x.chatId = -5;
                x.messageId = i++;
                x.text = "live";
                store->insertRawMessage(x);
            }
        });
        auto plain = db::createBackup(*store, dir + L"\\a.srbackup", db::ExportScope::FullHistory, "", cfg);
        auto enc = db::createBackup(*store, dir + L"\\b.srbackup", db::ExportScope::FullHistory, "correct horse battery", cfg);
        stop = true;
        writer.join();
        CHECK(plain.ok());
        CHECK(enc.ok());
        auto pv = db::stageImport(dir + L"\\a.srbackup", "", dir + L"\\stage1");
        CHECK(pv.ok());
        CHECK_EQ(pv.value().integrity, std::string("ok"));
        CHECK(!db::stageImport(dir + L"\\b.srbackup", "wrong password", dir + L"\\stage2").ok());
        auto pv2 = db::stageImport(dir + L"\\b.srbackup", "correct horse battery", dir + L"\\stage3");
        CHECK(pv2.ok());
        // Machine-bound account binding excluded from the archive.
        db::Database staged;
        CHECK(staged.open(platform::narrow(pv2.value().stagedDbPath), true).ok());
        auto q = staged.prepare("SELECT COUNT(*) FROM account_bindings");
        CHECK(q.ok() && q.value().step() && q.value().i64(0) == 0);
        // Tamper: flip a byte near the end.
        auto bytes = platform::readFileUtf8(dir + L"\\a.srbackup").value();
        bytes[bytes.size() - 10] ^= 0x5A;
        platform::writeFileAtomic(dir + L"\\c.srbackup", bytes);
        CHECK(!db::stageImport(dir + L"\\c.srbackup", "", dir + L"\\stage4").ok());
        platform::writeFileAtomic(dir + L"\\d.srbackup", "garbage");
        CHECK(!db::stageImport(dir + L"\\d.srbackup", "", dir + L"\\stage5").ok());
    }
    SetEnvironmentVariableW(L"PIPKITE_DATA_DIR", nullptr);
    fs::remove_all(dir);
}

TEST(framing_partial_reads_and_malformed) {
    std::string a = ipc::encodeFrame("{\"x\":1}");
    std::string b = ipc::encodeFrame("{\"y\":2}");
    std::string both = a + b;
    ipc::FrameDecoder dec;
    std::string out;
    int frames = 0;
    for (char c : both) {  // byte-by-byte delivery
        dec.feed(&c, 1);
        while (dec.next(out) == ipc::FrameDecoder::State::Frame) ++frames;
    }
    CHECK_EQ(frames, 2);
    CHECK_EQ(out, std::string("{\"y\":2}"));
    ipc::FrameDecoder bad;
    const char huge[4] = {0x00, 0x00, 0x10, 0x00};  // 1 MiB length -> rejected before allocation
    bad.feed(huge, 4);
    CHECK(bad.next(out) == ipc::FrameDecoder::State::Malformed);
    CHECK(ipc::encodeFrame(std::string(70000, 'a')).empty());
}

TEST(envelope_schema_validation) {
    ipc::Envelope e;
    e.type = ipc::msg::EXECUTE;
    e.id = newUuid();
    e.seq = 7;
    e.payload = {{"a", 1}};
    auto parsed = ipc::parseEnvelope(e.serialize());
    CHECK(parsed.has_value());
    CHECK_EQ(parsed->seq, 7);
    CHECK(!ipc::parseEnvelope("{\"v\":2,\"type\":\"X\"}").has_value());
    CHECK(!ipc::parseEnvelope("{\"v\":1,\"type\":\"X\",\"payload\":[1]}").has_value());
    CHECK(!ipc::parseEnvelope("[]").has_value());
    CHECK(!ipc::parseEnvelope("{\"v\":1,\"type\":\"X\",\"seq\":\"1\"}").has_value());
}

TEST(crypto_primitives) {
    auto kp = crypto::ecdsaGenerate();
    CHECK(kp.has_value());
    auto sig = crypto::ecdsaSign(kp->privateBlob, "manifest");
    CHECK(sig.has_value());
    CHECK(crypto::ecdsaVerify(kp->publicBlob, "manifest", *sig));
    CHECK(!crypto::ecdsaVerify(kp->publicBlob, "manifest2", *sig));
    auto h = crypto::hmacSha256(crypto::Bytes{1, 2, 3}, "abc");
    CHECK_EQ(h.size(), std::size_t(32));
    CHECK_EQ(crypto::toHex(crypto::sha256("abc")), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    auto b64 = crypto::toBase64(crypto::Bytes{0, 1, 2, 250});
    CHECK(crypto::fromBase64(b64).value() == (crypto::Bytes{0, 1, 2, 250}));
    auto prot = crypto::dpapiProtect(crypto::Bytes{9, 9, 9}, L"test");
    CHECK(prot.has_value());
    CHECK(crypto::dpapiUnprotect(*prot).value() == (crypto::Bytes{9, 9, 9}));
    auto box = crypto::sealWithPassword(crypto::Bytes{1, 2, 3, 4}, "password123", 100000);
    CHECK(box.has_value());
    CHECK(crypto::openWithPassword(*box, "password123").value() == (crypto::Bytes{1, 2, 3, 4}));
    CHECK(!crypto::openWithPassword(*box, "password124").has_value());
}

TEST(named_pipe_roundtrip_current_user_only) {
    const std::wstring name = platform::pipeName(L"test-" + std::to_wstring(GetCurrentProcessId()));
    std::atomic<int> received{0};
    ipc::PipeServer server(name, 4, [&](std::shared_ptr<ipc::PipeConnection> c) {
        c->readLoop([&](std::string&& payload) {
            ++received;
            c->send("echo:" + payload);
        });
    });
    CHECK(server.start());
    std::string err;
    auto client = ipc::connectPipe(name, 3000, &err);
    CHECK(client != nullptr);
    std::atomic<int> echoes{0};
    std::thread reader([&] { client->readLoop([&](std::string&& p) { if (p.rfind("echo:", 0) == 0) ++echoes; }); });
    for (int i = 0; i < 50; ++i) CHECK(client->send("{\"n\":" + std::to_string(i) + "}"));
    for (int i = 0; i < 100 && echoes < 50; ++i) Sleep(20);
    CHECK_EQ(echoes.load(), 50);
    client->close();
    reader.join();
    server.stop();
    CHECK_EQ(received.load(), 50);
}
