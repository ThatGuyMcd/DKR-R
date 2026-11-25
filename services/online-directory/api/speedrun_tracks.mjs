// The 20 Time Trial courses and vehicle combinations follow the game's save records.
const worlds = [
  ['Dino Domain', [
    ['Ancient Lake', 'chp'], ['Fossil Canyon', 'chp'], ['Jungle Falls', 'chp'], ['Hot Top Volcano', 'hp'],
  ]],
  ['Snowflake Mountain', [
    ['Everfrost Peak', 'chp'], ['Walrus Cove', 'ch'], ['Snowball Valley', 'ch'], ['Frosty Village', 'chp'],
  ]],
  ['Sherbet Island', [
    ['Whale Bay', 'h'], ['Crescent Island', 'ch'], ['Pirate Lagoon', 'h'], ['Treasure Caves', 'chp'],
  ]],
  ['Dragon Forest', [
    ['Windmill Plains', 'chp'], ['Greenwood Village', 'ch'], ['Boulder Canyon', 'h'], ['Haunted Woods', 'ch'],
  ]],
  ['Future Fun Land', [
    ['Spacedust Alley', 'chp'], ['Darkmoon Caverns', 'ch'], ['Spaceport Alpha', 'chp'], ['Star City', 'chp'],
  ]],
];

const vehicles = { c: 'car', h: 'hovercraft', p: 'plane' };

export const vanillaTracks = worlds.flatMap(([world, courses]) => courses.map(([title, codes]) => ({
  id: title.toLowerCase().replaceAll(' ', '-'), title, world,
  vehicles: Array.from(codes, code => vehicles[code]),
})));

