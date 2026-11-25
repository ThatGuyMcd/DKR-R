import { ADVENTURE_RULES, CUSTOM_RULES, CUSTOM_VARIABLE_LAPS_RULES, adventureCategories, ancientLakeRacers,
  replayHash, replayId } from '../../../runtime-recomp/src/game/mod_library/website/assets/replay-format.js';
import { vanillaTracks } from './speedrun_tracks.mjs';

export const legacyRules = 'ancient-lake-us80-practice-2';
export const vanillaRules = 'vanilla-time-trial-us80-1';
export const customRules = [CUSTOM_RULES, CUSTOM_VARIABLE_LAPS_RULES];
export const customPackageJoin = `LEFT JOIN speedrun_custom_packages q
  ON b.kind='custom' AND q.release_id=b.release_id AND q.release_sha256=b.release_sha256
  LEFT JOIN launcher_release_integrity i ON i.release_id=q.release_id AND i.sha256=q.release_sha256`;
export const boardColumns = 'b.*, CASE WHEN i.release_id IS NOT NULL THEN q.track_id END AS track_id, q.track_sha256, q.lap_count';
const legacyPrefix = 'ancient-lake-car';

export function vanillaBoard(row) {
  if (!row || row.kind !== 'vanilla' || ![legacyRules,vanillaRules].includes(row.ruleset)) return null;
  const track = vanillaTracks.find(track => track.title === row.title);
  if (!track?.vehicles.includes(row.vehicle)) return null;
  const legacyCourse = track.id === 'ancient-lake' && row.vehicle === 'car';
  if (legacyCourse !== (row.ruleset === legacyRules)) return null;
  const prefix = track.id + '-' + row.vehicle;
  const racer = ancientLakeRacers.findIndex(([id]) => prefix + id.slice(legacyPrefix.length) === row.id);
  if (racer < 0) return null;
  return { course:track.id, vehicle:row.vehicle, racer, name:ancientLakeRacers[racer][1] };
}

export function matchesVanillaBoard(manifest, row) {
  const route = vanillaBoard(row);
  if (!route || route.racer !== (manifest.racer ?? 9) || row.ruleset !== manifest.ruleset ||
      row.engine !== manifest.engine || row.release_sha256 !== manifest.releaseHash) return false;
  return manifest.ruleset === legacyRules || (manifest.course === route.course && manifest.vehicle === route.vehicle);
}

export function customBoard(row) {
  if (!row || row.kind !== 'custom' || !Number.isInteger(row.lap_count) || row.lap_count < 1 || row.lap_count > 5 ||
      row.ruleset !== (row.lap_count === 3 ? CUSTOM_RULES : CUSTOM_VARIABLE_LAPS_RULES) || !replayId(row.release_id) ||
      !replayHash(row.release_sha256) || !replayId(row.track_id) || !replayHash(row.track_sha256) ||
      !['car','hovercraft','plane'].includes(row.vehicle)) return null;
  const prefix = 'track-' + row.release_id + '-' + row.vehicle;
  const racer = ancientLakeRacers.findIndex(([id]) => prefix + id.slice(legacyPrefix.length) === row.id);
  if (racer < 0) return null;
  return { trackId:row.track_id, trackHash:row.track_sha256, vehicle:row.vehicle, laps:row.lap_count, racer,
    name:ancientLakeRacers[racer][1] };
}

export const adventureRules = ADVENTURE_RULES;

export function adventureBoard(row) {
  return row?.kind === 'full-game' && row.ruleset === adventureRules && row.vehicle === 'mixed' &&
    adventureCategories.includes(row.category) && row.id === 'adventure-' + row.category;
}

export function matchesRankedBoard(manifest, row, allowCustom, allowAdventure) {
  if (row?.kind === 'full-game') return allowAdventure && adventureBoard(row) &&
    manifest.ruleset === adventureRules && manifest.engine === row.engine &&
    manifest.releaseHash === null && manifest.category === row.category;
  if (row?.kind !== 'custom') return matchesVanillaBoard(manifest, row);
  const route = allowCustom && customBoard(row);
  return !!route && manifest.ruleset === row.ruleset && manifest.engine === row.engine &&
    manifest.releaseHash === row.release_sha256 && manifest.trackId === route.trackId &&
    manifest.trackHash === route.trackHash && manifest.vehicle === route.vehicle && (manifest.laps ?? 3) === route.laps &&
    (manifest.racer ?? 9) === route.racer;
}
