import app/world

/// `world` is a local here: `world.turn` is its field, `world.allows` the module's function.
pub fn then(world: world.World) -> Bool {
  world.turn > 1 && world.allows(world)
}

/// `apply` is a local closure here, never this module's `apply` nor `world.apply`.
pub fn chain(f: fn(Int) -> Int) -> Int {
  let apply = f
  apply(1)
}

pub fn apply(x: Int) -> Int {
  x
}
