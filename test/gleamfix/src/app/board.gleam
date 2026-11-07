//// The board: where the pieces stand.

import app/world.{type World}

/// One square of the board.
pub type Cell {
  Empty
  Taken(by: String)
}

pub opaque type Board {
  Board(cells: List(Cell))
}

pub type Grid =
  List(Cell)

pub const size = 8

/// Put a piece on the board.
pub fn place(board: Board, who: String) -> Board {
  Board(cells: [Taken(who), ..board.cells])
}

pub fn new() -> Board {
  Board(cells: [])
}

/// The host clock.
@external(erlang, "os", "system_time")
@external(javascript, "./ffi.mjs", "now")
pub fn now() -> Int

fn hidden(w: World) -> World {
  world.apply(w, size)
}
