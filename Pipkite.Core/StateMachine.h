#pragma once
// Command lifecycle transitions. Signal, request, order and deal states stay separate.

#include "Models.h"

namespace sr {

bool isTerminal(CommandState s);
bool canTransition(CommandState from, CommandState to);
// True when the command has crossed the durable submission boundary (EA may have sent it to the broker).
bool crossedSubmissionBoundary(CommandState s);

}  // namespace sr
