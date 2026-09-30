#pragma once

// Every pane (terminal or view) has an id from one sequence, so pane ids are unique across kinds and the
// layout tree, the API and the session file can refer to any pane by id alone.
namespace PaneIds {
inline int &counter() { static int next = 1; return next; }
inline int next() { return counter()++; }
inline void reserve(int id) { if (id >= counter()) counter() = id + 1; }
} // namespace PaneIds
