#include "Interfaces.h"

namespace sr {

const char* toString(TelegramAuthState s) {
    switch (s) {
        case TelegramAuthState::NotConfigured: return "NotConfigured";
        case TelegramAuthState::WaitParameters: return "WaitParameters";
        case TelegramAuthState::WaitPhone: return "WaitPhone";
        case TelegramAuthState::WaitCode: return "WaitCode";
        case TelegramAuthState::WaitPassword: return "WaitPassword";
        case TelegramAuthState::Ready: return "Ready";
        case TelegramAuthState::LoggingOut: return "LoggingOut";
        case TelegramAuthState::Closed: return "Closed";
        case TelegramAuthState::LibraryMissing: return "LibraryMissing";
        case TelegramAuthState::Error: return "Error";
    }
    return "Error";
}

}  // namespace sr
