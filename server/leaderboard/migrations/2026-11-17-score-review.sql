-- Score review, for a database made before it: run once, then schema.sql (which adds the new tables and is safe to run
-- again). New databases get the column from schema.sql itself.
ALTER TABLE scores ADD COLUMN verified INTEGER NOT NULL DEFAULT 0;
