pub type World {
  World(turn: Int)
}

/// Advance the world by some turns.
pub fn apply(world: World, by: Int) -> World {
  World(turn: world.turn + by)
}

pub fn allows(world: World) -> Bool {
  world.turn > 0
}
