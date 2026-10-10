#include "StateMachine.h"

namespace sr {

bool isTerminal(CommandState s) {
    return s == CommandState::Filled || s == CommandState::Rejected || s == CommandState::Canceled || s == CommandState::Expired;
}

bool crossedSubmissionBoundary(CommandState s) {
    return s != CommandState::Prepared && s != CommandState::Rejected;
}

bool canTransition(CommandState from, CommandState to) {
    using S = CommandState;
    if (from == to) return from == S::PartiallyFilled;  // repeated partial fills are legitimate
    if (isTerminal(from)) return false;
    switch (from) {
        case S::Prepared: return to == S::Dispatching || to == S::Rejected || to == S::Canceled;
        case S::Dispatching: return to == S::ReceivedByEA || to == S::Rejected || to == S::Unknown;
        case S::ReceivedByEA: return to == S::Submitted || to == S::Rejected || to == S::Unknown;
        case S::Submitted:
            return to == S::Accepted || to == S::Rejected || to == S::Unknown || to == S::PartiallyFilled || to == S::Filled || to == S::Working;
        case S::Accepted: return to == S::PartiallyFilled || to == S::Filled || to == S::Working || to == S::Rejected || to == S::Canceled || to == S::Unknown;
        case S::Working: return to == S::PartiallyFilled || to == S::Filled || to == S::Canceled || to == S::Expired || to == S::Unknown;
        case S::PartiallyFilled: return to == S::Filled || to == S::Canceled || to == S::Expired || to == S::Unknown;
        case S::Unknown:
            // Only reconciliation evidence moves an Unknown command; never back to a pre-submission state.
            return to == S::Accepted || to == S::Filled || to == S::PartiallyFilled || to == S::Rejected || to == S::Working || to == S::Canceled || to == S::Expired;
        default: return false;
    }
}

}  // namespace sr
