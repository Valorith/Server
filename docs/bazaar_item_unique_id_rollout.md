# Bazaar Item Unique ID And World-Only Offline Trading Rollout

## Purpose

This rollout converts persisted item identity and offline trader or buyer session state to the new production-safe model. The migration is designed for a maintenance window and explicitly clears any in-flight trader, buyer, and offline sessions during cutover.

Offline trader and buyer reconnect now use the normal world and zone flow. No custom loginserver support is required. World checks for an indexed `offline_character_sessions` row during character entry and only pauses login when it must reclaim an active offline trader or buyer session.

Do not reopen the server until every verification step passes.

## Preconditions

- Schedule a maintenance window.
- Stop new logins before beginning the migration.
- Ensure the `world` binary you are deploying includes this branch.
- Ensure the deployed `world` and `zone` binaries are from the same build.
- A stock or public loginserver is supported; no custom loginserver rollout is required.
- Ensure operators have credentials to run schema updates and database dump commands.

## Mandatory Backup

Take a backup before any schema or migration command:

```powershell
world database:dump --player-tables --login-tables --dump-path=backups --compress
```

If you use a separate database backup process, complete it before continuing.

## Local Dev Validation

Run these commands against the local dev database before production:

```powershell
world database:updates --skip-backup
world database:item-unique-ids --preflight --verbose
world database:item-unique-ids --migrate --verbose
world database:item-unique-ids --verify --verbose
```

Validate these gameplay scenarios after the migration:

- Two traders listing the same item at different prices.
- One trader changing a price without affecting another trader.
- Offline trader purchase.
- Offline buyer purchase.
- Relog to the same account while an offline trader is active and verify entry resumes after reclaim.
- Log into a different character on the same account while an offline buyer is active and verify the offline session is ended before entry continues.
- Confirm an unresponsive reclaim path fails the character-entry attempt within 10 seconds.
- Parcel retrieval for rows that previously had missing `item_unique_id` values.
- Alternate bazaar shard search.

## Production Sequence

1. Bring the server into maintenance mode and stop new gameplay activity.
2. Take the mandatory backup.
3. Apply schema updates:

```powershell
world database:updates
```

4. Run the migration preflight:

```powershell
world database:item-unique-ids --preflight --verbose
```

5. If preflight reports missing schema, duplicate live item IDs, or other unexpected errors, stop and resolve them before continuing.
6. Run the migration:

```powershell
world database:item-unique-ids --migrate --verbose
```

This step clears active trader, buyer, and offline session state. Players must re-enter those modes after deploy.

7. Run final verification:

```powershell
world database:item-unique-ids --verify --verbose
```

8. Reopen the server only after verification passes.

## Login Performance Expectation

- Character entry with no offline session should take the normal fast path and only add one indexed lookup by `account_id`.
- Character entry with an offline trader or buyer session should target exactly one owning zone or instance.
- If the owning zone is down, world clears the stale session locally and continues immediately.
- If the owning zone does not answer a reclaim request, world fails the character-entry attempt after 10 seconds instead of hanging indefinitely.

## What Preflight And Verify Must Show

The migration is not complete unless all of the following are true:

- `inventory`, `sharedbank`, and `trader` contain no null or blank `item_unique_id` values.
- `character_parcels`, `character_parcels_containers`, and `inventory_snapshots` contain no null or blank `item_unique_id` values.
- `inventory`, `sharedbank`, and `trader` contain no duplicate `item_unique_id` groups.
- There are no cross-table duplicate live `item_unique_id` values across `inventory`, `sharedbank`, and `trader`.
- `offline_character_sessions` is empty after migration.
- `account.offline` has no rows left set to `1`.

## Rollback Criteria

Rollback instead of reopening the server if any of the following occur:

- `world database:updates` fails.
- `world database:item-unique-ids --preflight` reports missing required schema.
- `world database:item-unique-ids --migrate` or `--verify` reports missing IDs, duplicate IDs, or stale offline session state.
- Bazaar listing, offline trader, offline buyer, or parcel retrieval smoke tests fail after migration.

## Rollback Actions

1. Keep the server in maintenance mode.
2. Restore the database backup taken before the rollout.
3. Redeploy the previous server build.
4. Confirm login, trader, buyer, and parcel behavior on the restored build before reopening the server.

## Notes

- `world database:item-unique-ids --keep-trading-state` exists for non-production diagnostics only. Do not use it during production cutover.
- The migration command expects schema updates to have been applied first. If required tables or columns are missing, the command fails and should not be bypassed.
