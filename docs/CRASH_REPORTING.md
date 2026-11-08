# Automatic crash reporting

Reporting is opt-in. The launcher asks once and saves either answer in
`crash-reports/consent.json` inside its active data folder. About DKR-R > Support
contains the same setting in the launcher and overlay. The prompt blocks an
automatic or manual game launch until a choice is made. Reporting does not
require a Discord login or a DKR-R account.

## What is collected

On the next launch after a crash, the service recognises the crashed session's
`[boot][crash]` marker in `logs/runtime-previous.log`. The older spelling
`previous-runtime.log` is accepted when the normal file does not exist. It
freezes bounded snapshots of that log and the new `runtime.log` into a ZIP.
Only crashes after reporting was enabled are eligible. Existing historical
crashes are not uploaded when somebody first opts in.

Text redaction removes common local paths, URLs, IP addresses, email addresses,
local user/machine names and lines mentioning account, chat or authentication
data. This is **best effort, not a guarantee of anonymity**. Free-form log
content may still reveal information. The archive contains no separately
attached saves, ROMs, account database, settings file or mod packages. It has a
per-report ID, application version and an inclusion/omission manifest, without
a persistent installation or player ID.

Raw memory dumps have a **separate, default-off consent**. Only the exact
`DKR-R-crash-YYYYMMDD-HHMMSS.dmp` named in the crashed session's log is eligible.
These dumps can contain personal data, credentials and other process memory;
they cannot reliably be anonymised. Linux currently writes the crash backtrace
to its log, so that is included; the reporter does not search system-wide core
dump folders. A dump over 32 MiB, or an archive over 8 MiB after compression,
falls back to logs with the reason recorded in the manifest. The original dump
stays on the player's machine for a separate, reviewed support request.

## Queue and lifetime

The process-owned worker runs outside the crash handler and UI thread. It
pauses uploads during gameplay, checks pending work every 30 seconds while the
launcher is open, and resumes on subsequent launches. There is no background
service when DKR-R is closed. Connection failures, unconfirmed responses,
HTTP 408/429 and server errors retain the same frozen report, with persisted
exponential backoff (30 seconds to one hour) and server Retry-After delays.
Other client errors stop automatic retries for that entry and show a support
status. A successful response must confirm the same report ID before deletion.

The private queue holds up to eight reports of at most 8 MiB each, expiring
after 30 days. Originals are never deleted. Turning reporting off cancels an
in-flight upload and removes unsent reports in the worker; bytes already sent
cannot be recalled. Turning off dump sharing removes dumps from queued reports
before any later delivery. Queue operations use a process lock, reject linked
inputs and accept only fixed archive names and bounded metadata. Interrupted
enqueue/acknowledgement cleanup removes only strictly named orphan archives.

## Private Discord relay

The client posts an archive over verified HTTPS to the dedicated
`dkr-crash-reports` Cloudflare Worker. No Discord credential is in source,
client binaries or release packages. The backend source and deployment
configuration are in `runtime-recomp/src/game/crash_reports/backend/`.

Deploy that dedicated Worker in the same Cloudflare account/subdomain as the
existing DKR-R API; it does not replace the Mod Library or online service.
Configure `DKR_CRASH_WEBHOOK` using **Wrangler secret put**, pasting the webhook
through the hidden prompt/stdin. Do not add it to a config file, command-line
argument, source archive, client setting or public hosting folder. The Worker
requires its Durable Object and rate-limiter bindings. `/health` reports only
the service/schema; it does not disclose the secret or accept a report.

The relay limits request/archive sizes, fixed entry names, per-IP ingress and
global daily delivery. The IP is used by Cloudflare's ephemeral limiter and is
not stored in diagnostic receipts or forwarded to Discord. Cloudflare and
Discord still process network metadata and uploaded content; network uploads
cannot honestly be described as 100% anonymous. Worker observability is off.

Discord receives a ZIP attachment with mentions disabled. The relay uses
`wait=true`, requires a returned message ID and records a 31-day receipt before
acknowledging the client. Ordinary retries are deduplicated by report ID and
archive digest. Discord has no idempotency key: a connection/process failure
after Discord accepts a message but before a receipt is saved can still cause
a duplicate bearing the same report ID. Rate limits respect Discord's returned
delay. A revoked/invalid webhook is suspended until its server secret changes.

## Verification

`DKRCrashReports` covers consent, no retroactive collection, redaction,
immutable offline snapshots, restart/backoff, confirmed acknowledgements,
duplicate suppression, dump revocation, linked inputs, queue bounds and expiry.
`DKRCrashRelay` uses synthetic archives and a mocked Discord transport to cover
admission, confirmation, concurrency, rate limits, network failures and revoked
credentials. Live smoke tests must use synthetic data only. Never use another
player's supplied logs or dump as an upload fixture without their consent.

New consent/status copy is source English pending translation review in the
embedded catalogues; the privacy claims must remain accurate when translated.
