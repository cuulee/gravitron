open GravShared;

open SharedTypes;

open MyUtils;

open Reprocessing;

let arrowAccs = {
  let speed = 0.3;
  [
    (Events.Left, vecFromPos((-. speed, 0.))),
    (Events.Up, vecFromPos((0., -. speed))),
    (Events.Down, vecFromPos((0., speed))),
    (Events.Right, vecFromPos((speed, 0.)))
  ]
};

let floatPos = ((a, b)) => (float_of_int(a), float_of_int(b));

let clampVec = (vel, min, max, fade) =>
  vel.mag > max ?
    {...vel, mag: max} : vel.mag < min ? {...vel, mag: 0.} : {...vel, mag: vel.mag *. fade};

let springToward = (p1, p2, scale) => {
  let vec = vecToward(p1, p2);
  {...vec, mag: vec.mag *. scale}
};

let randomTarget = (w, h) => {
  let margin = 30.;
  (Random.float(w -. margin *. 2.) +. margin, Random.float(h -. margin *. 2.) +. margin)
};

let wallSize = 10.;

type offscreen = Left | Top | Right | Bottom | OnScreen;
let offscreen = ((x, y), w, h, size) => {
  let size = size + int_of_float(wallSize);
  let x = int_of_float(x);
  let y = int_of_float(y);
  if (x - size < 0) Left
  else if (y - size < 0) Top
  else if (x + size > w) Right
  else if (y + size > h) Bottom
  else OnScreen
};

let bounceVel = (vel, off) => {
  mag: off === OnScreen ? vel.mag : vel.mag *. 0.5,
  theta: switch off {
  | OnScreen => vel.theta
  | Left | Right => if (vel.theta < Constants.pi) {Constants.pi -. vel.theta} else { Constants.pi *. 3. -. vel.theta }
  | Top | Bottom => Constants.two_pi -. vel.theta
  }
};

let keepOnScreen = ((x, y), w, h, size) => {
  let size = size +. wallSize;
  (max(size, min(x, w -. size)), max(size, min(y, h -. size)))
};

let bouncePos = (wallType, vel, pos, w, h, delta, size) => {
  let off = offscreen(pos, w, h, int_of_float(size));
  switch (wallType, off) {
    | (Minimapped, _)
    | (_, OnScreen) => (vel, posAdd(pos, vecToPos(scaleVec(vel, delta))))
    | _ =>
      let vel = bounceVel(vel, off);
      (vel, keepOnScreen(posAdd(pos, vecToPos(scaleVec(vel, delta))), float_of_int(w), float_of_int(h), size))
  }
};

let deltaTime = (env) => Env.deltaTime(env) *. 1000. /. 16.;

let stepMeMouse = ({me} as state, env) =>
  Player.(
    if (Env.mousePressed(env)) {
      let delta = deltaTime(env);
      let mousePos = floatPos(Reprocessing_Env.mouse(env));
      let mousePos = isPhone ? scalePos(mousePos, phoneScale) : mousePos;
      let vel = springToward(me.pos, mousePos, 0.1);
      let vel = clampVec(vel, 0.01, 7., 0.98);
      let (vel, pos) = bouncePos(state.wallType, vel, me.pos, Env.width(env), Env.height(env), delta, me.size);
      {...state, me: {...me, pos, vel}, hasMoved: true}
    } else {
      state
    }
  );

let stepMeJoystick = ({me} as state, env) =>
  Player.(
    if (Env.mousePressed(env)) {
      let vel = springToward(joystickPos(env), floatPos(Reprocessing_Env.mouse(env)), 0.1);
      let vel = clampVec(vel, 1., 7., 0.98);
      let delta = deltaTime(env);
      let (vel, pos) = bouncePos(state.wallType, vel, me.pos, Env.width(env), Env.height(env), delta, me.size);
      /* let pos = posAdd(me.pos, vecToPos(scaleVec(vel, delta))); */
      {...state, me: {...me, pos, vel}, hasMoved: true}
    } else {
      state
    }
  );

let stepMeKeys = ({me} as state, env) => {
  open Player;
  let vel =
    List.fold_left(
      (acc, (key, acc')) => Env.key(key, env) ? vecAdd(acc, acc') : acc,
      me.vel,
      arrowAccs
    );
  let vel = clampVec(vel, 0.01, 7., 0.98);
  let delta = Env.deltaTime(env) *. 1000. /. 16.;
  let (vel, pos) = bouncePos(state.wallType, vel, me.pos, Env.width(env), Env.height(env), delta, me.size);
  {...state, me: {...me, pos, vel},
    hasMoved: state.hasMoved || vel.mag > 0.01
  }
};

let stepEnemy = (env, state, enemy) => {
  open Enemy;
  let (warmup, loaded) = stepTimer(enemy.warmup, env);
  if (! loaded) {
    {...state, enemies: [{...enemy, warmup}, ...state.enemies]}
  } else if (collides(enemy.pos, state.me.Player.pos, enemy.size +. state.me.Player.size)) {
    {
      ...state,
      status: Dead(100),
      explosions: [playerExplosion(state.me), enemyExplosion(enemy), ...state.explosions]
    }
  } else {
    let enemy =
      switch enemy.movement {
      | Stationary => enemy
      | Wander(target, vel) =>
        let vel = vecAdd(vel, {theta: thetaToward(enemy.pos, target), mag: 0.01});
        let vel = {theta: vel.theta, mag: min(vel.mag, 4.) *. 0.98};
        let pos = posAdd(enemy.pos, vecToPos(vel));
        let target =
          collides(enemy.pos, target, enemy.size *. 2.)
          ? randomTarget(Env.width(env) |> float_of_int, Env.height(env) |> float_of_int)
          : target;
        {...enemy, pos, movement: Wander(target, vel)}
      | GoToPosition(target, vel) =>
        let vel = vecAdd(vel, {theta: thetaToward(enemy.pos, target), mag: 0.01});
        let vel = {theta: vel.theta, mag: min(vel.mag, 2.) *. 0.98};
        let pos = posAdd(enemy.pos, vecToPos(vel));
        {...enemy, pos, movement: GoToPosition(target, vel)}
      };
    switch enemy.behavior {
    | TripleShooter(timer, bulletConfig) =>
      let (timer, looped) = loopTimer(timer, env);
      if (looped) {
        {
          ...state,
          bullets: [
            shoot(~theta=0.3, bulletConfig, env, enemy, state.me),
            shoot(~theta=(-0.3), bulletConfig, env, enemy, state.me),
            shoot(~theta=0., bulletConfig, env, enemy, state.me),
            ...state.bullets
          ],
          enemies: [
            {...enemy, warmup, behavior: TripleShooter(timer, bulletConfig)},
            ...state.enemies
          ]
        }
      } else {
        {
          ...state,
          enemies: [
            {...enemy, warmup, behavior: TripleShooter(timer, bulletConfig)},
            ...state.enemies
          ]
        }
      }
    | ScatterShot(timer, count, bulletConfig, subConfig) =>
      let (timer, looped) = loopTimer(timer, env);
      if (looped) {
        {
          ...state,
          bullets: [
            shoot(~behavior=Scatter(count, (0., 40.), subConfig), bulletConfig, env, enemy, state.me),
            ...state.bullets
          ],
          enemies: [
            {...enemy, warmup, behavior: ScatterShot(timer, count, bulletConfig, subConfig)},
            ...state.enemies
          ]
        }
      } else {
        {
          ...state,
          enemies: [
            {...enemy, warmup, behavior: ScatterShot(timer, count, bulletConfig, subConfig)},
            ...state.enemies
          ]
        }
      }
    | Asteroid(timer, animate, bulletConfig) =>
      let (timer, looped) = loopTimer(timer, env);
      let animate = animate +. increment(env);
      if (looped) {
        {
          ...state,
          bullets: [shoot(bulletConfig, env, enemy, state.me), ...state.bullets],
          enemies: [
            {...enemy, warmup, behavior: Asteroid(timer, animate, bulletConfig)},
            ...state.enemies
          ]
        }
      } else {
        {
          ...state,
          enemies: [
            {...enemy, warmup, behavior: Asteroid(timer, animate, bulletConfig)},
            ...state.enemies
          ]
        }
      }
    | SimpleShooter(timer, bulletConfig) =>
      let (timer, looped) = loopTimer(timer, env);
      if (looped) {
        {
          ...state,
          bullets: [shoot(bulletConfig, env, enemy, state.me), ...state.bullets],
          enemies: [
            {...enemy, warmup, behavior: SimpleShooter(timer, bulletConfig)},
            ...state.enemies
          ]
        }
      } else {
        {
          ...state,
          enemies: [
            {...enemy, warmup, behavior: SimpleShooter(timer, bulletConfig)},
            ...state.enemies
          ]
        }
      }
    }
  }
};

let stepEnemies = (state, env) =>
  List.fold_left(stepEnemy(env), {...state, enemies: []}, state.enemies);

let moveBullet = (bullet, env) => {
  let delta = Env.deltaTime(env) *. 1000. /. 16.;
  Bullet.{...bullet, pos: posAdd(bullet.pos, vecToPos(scaleVec(bullet.vel, delta)))}
};

let bulletToBullet = (bullet, bullets, explosions) => {
  let (removed, bullets, explosions) =
    List.fold_left(
      ((removed, bullets, explosions), other) =>
        Bullet.(
          removed ?
            (true, [other, ...bullets], explosions) :
            collides(bullet.pos, other.pos, bullet.size +. other.size) ?
              (true, bullets, [bulletExplosion(bullet), bulletExplosion(other), ...explosions]) :
              (false, [other, ...bullets], explosions)
        ),
      (false, [], explosions),
      bullets
    );
  if (removed) {
    (bullets, explosions)
  } else {
    ([bullet, ...bullets], explosions)
  }
};

let asteroidSplitVel = () => {
  let theta = Random.float(Constants.two_pi);
  (
    {theta, mag: 1.5 +. Random.float(1.)},
    {
      theta: theta -. Constants.pi +. Random.float(Constants.pi /. 2.),
      mag: 1.5 +. Random.float(1.)
    }
  )
};

let bulletToEnemiesAndBullets = (bullet, state, env) => {
  let (hit, enemies, explosions) =
    List.fold_left(
      ((hit, enemies, explosions), enemy) =>
        hit ?
          (hit, [enemy, ...enemies], explosions) :
          (
            if (collides(enemy.Enemy.pos, bullet.Bullet.pos, enemy.Enemy.size +. bullet.Bullet.size)) {
              let (health, dead) = countDown(enemy.Enemy.health);
              if (dead) {
                (true, enemies, [enemyExplosion(enemy), bulletExplosion(bullet), ...explosions])
              } else {
                switch enemy.Enemy.behavior {
                | Asteroid(
                    (_, bulletTime),
                    animate,
                    (bulletColor, bulletSize, bulletSpeed, bulletDamage)
                  ) =>
                  let w = float_of_int(Env.width(env)) *. phoneScale;
                  let h = float_of_int(Env.height(env)) *. phoneScale;
                  let (current, _) = health;
                  let (one, two) = asteroidSplitVel();
                  let size = float_of_int(current) *. 5. +. 10.;
                  let smallerBullets = (
                    bulletColor,
                    float_of_int(2 + current * 2),
                    bulletSpeed,
                    3 + current * 3
                  );
                  /* let (one, two) = splitAsteroid(enemy.Enemy.pos, bulletTime, bulletConfig); */
                  (
                    true,
                    [
                      {
                        ...enemy,
                        size,
                        movement: GoToPosition(randomTarget(w, h), one),
                        behavior:
                          Asteroid(
                            (Random.float(bulletTime /. 4.), bulletTime),
                            animate,
                            smallerBullets
                          ),
                        health: (current, current)
                      },
                      {
                        ...enemy,
                        size,
                        movement: GoToPosition(randomTarget(w, h), two),
                        behavior: Asteroid((0., bulletTime), animate, smallerBullets),
                        health: (current, current)
                      },
                      ...enemies
                    ],
                    [bulletExplosion(bullet), ...explosions]
                  )
                | _ => (
                    true,
                    [{...enemy, health}, ...enemies],
                    [bulletExplosion(bullet), ...explosions]
                  )
                }
              }
            } else {
              (false, [enemy, ...enemies], explosions)
            }
          ),
      (false, [], state.explosions),
      state.enemies
    );
  if (hit) {
    {...state, enemies, explosions}
  } else {
    let (bullets, explosions) = bulletToBullet(bullet, state.bullets, state.explosions);
    {...state, bullets, explosions}
  }
};

let makeScatterBullets = (bullet, (color, size, speed, damage), count) => {
  open Bullet;
  let by = Constants.two_pi /. float_of_int(count);
  let rec loop = (i) =>
    i > 0 ?
      {
        let theta = by *. float_of_int(i);
        [
          {
            color,
            behavior: Normal,
            warmup: (0., 20.),
            size,
            pos: bullet.pos,
            vel: {mag: speed, theta},
            acc: v0,
            damage,
          },
          ...loop(i - 1)
        ]
      } :
      [];
  loop(count)
};

let stepBullets = (state, env) => {
  open Bullet;
  let player = state.me;
  let w = Env.width(env);
  let h = Env.height(env);
  List.fold_left(
    (state, bullet) =>
      switch state.status {
      | Paused
      | Dead(_) => bulletToEnemiesAndBullets(moveBullet(bullet, env), state, env)
      | Running =>
        let {theta, mag} = vecToward(bullet.pos, player.Player.pos);
        if (mag < bullet.size +. player.Player.size) {
          if (state.me.Player.health - bullet.damage > 0) {
            {
              ...state,
              me: {...state.me, health: state.me.Player.health - bullet.damage},
              explosions: [bulletExplosion(bullet), ...state.explosions]
            }
          } else {
            {
              ...state,
              status: Dead(100),
              me: {...state.me, health: 0},
              explosions: [playerExplosion(player), bulletExplosion(bullet), ...state.explosions]
            }
          }
        } else {
          let acc = {theta, mag: 20. /. mag};
          let vel = vecAdd(bullet.vel, acc);
          let pos = posAdd(bullet.pos, vecToPos(vel));
          let (warmup, isFull) = stepTimer(bullet.warmup, env);
          let (bullet, dead) = switch (state.wallType, offscreen(pos, w, h, int_of_float(bullet.size))) {
          | (_, OnScreen) => ({...bullet, acc, vel, pos}, false)
          | (FireWalls, _) => (bullet, true)
          | (BouncyWalls, off) => {
            let vel = bounceVel(vel, off);
            let pos = posAdd(bullet.pos, vecToPos(vel));
            ({...bullet, vel, pos}, false)
          }
          | _ => ({...bullet, acc, vel, pos}, false)
          };
          if (dead) {
            {...state, explosions: [bulletExplosion(bullet), ...state.explosions]}
          } else if (isFull) {
            switch bullet.behavior {
            | Normal => bulletToEnemiesAndBullets({...bullet, warmup}, state, env)
            | Scatter(count, timer, bulletConfig) =>
              let (timer, maxed) = stepTimer(timer, env);
              if (maxed) {
                {...state, bullets: makeScatterBullets(bullet, bulletConfig, count) @ state.bullets}
              } else {
                bulletToEnemiesAndBullets(
                  {...bullet, behavior: Scatter(count, timer, bulletConfig), warmup},
                  state,
                  env
                )
              }
            }
          } else {
            {...state, bullets: [{...bullet, warmup}, ...state.bullets]}
          }
        }
      },
    {...state, bullets: []},
    state.bullets
  )
};

let stepExplosions = (explosions, env) =>
  Explosion.(
    List.fold_left(
      (explosions, {timer} as explosion) => {
        let (timer, finished) = stepTimer(timer, env);
        finished ? explosions : [{...explosion, timer}, ...explosions]
      },
      [],
      explosions
    )
  );