import app/board.{Taken, place as put}
import app/world as w
import gleam/list
import gleam/result

pub fn update(state: w.World, who: String) -> Result(w.World, Nil) {
  use _ <- result.try(check(state))
  let b = board.new() |> put(who)
  let _ = Taken(who)
  let boards = list.map([b], board.place(_, who))
  let next = state |> w.apply(1)
  Ok(list.fold(boards, next, fn(acc, _) { acc }))
}

fn check(state: w.World) -> Result(Nil, Nil) {
  case w.allows(state) {
    True -> Ok(Nil)
    False -> Error(Nil)
  }
}
