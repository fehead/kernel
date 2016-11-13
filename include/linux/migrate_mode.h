#ifndef MIGRATE_MODE_H_INCLUDED
#define MIGRATE_MODE_H_INCLUDED
/*
 * MIGRATE_ASYNC means never block
 * MIGRATE_SYNC_LIGHT in the current implementation means to allow blocking
 *	on most operations but not ->writepage as the potential stall time
 *	is too significant
 * MIGRATE_SYNC will block when migrating pages
 */
enum migrate_mode {
/* IAMROOT-12 fehead (2016-11-12):
 * --------------------------
 * MIGRATE_ASYNC 이주할때 나중에 동기화 메커니즘이 돌아간다.
 * MIGRATE_SYNC 이주(MIGRATE)할때 당장 동기화 메커니즘을 동작시킨다.
 */
	MIGRATE_ASYNC,
	MIGRATE_SYNC_LIGHT,
	MIGRATE_SYNC,
};

#endif		/* MIGRATE_MODE_H_INCLUDED */
