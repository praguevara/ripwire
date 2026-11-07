import app/turn
import app/world
import gleeunit/should

pub fn update_test() {
  turn.update(world.World(turn: 1), "a")
  |> should.be_ok
}
