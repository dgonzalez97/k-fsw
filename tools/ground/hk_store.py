#!/usr/bin/env python3
"""A durable home for housekeeping that came down, so a pass outlives the console.

Two things decide the shape of this store, and both come from how housekeeping
works on the node.

A frame carries values and no names: the ground decodes it with the report
definition. So the definition is the schema, and it is stored here beside the
samples. Every sample points at the definition that was in force when it
arrived, and a definition that changes gets a new row, so a sample recorded
under the old one is never decoded with the new one.

Every sample carries a sequence number so that a missing one is visible. This
store never fills a gap in and never interpolates: it keeps what was heard, and
the `gap` view reports what was not. A row is one reception, not one unique
sample, because a duplicate is evidence too.
"""

import hashlib
import sqlite3
import struct

SAMPLE_HEADER = struct.Struct(">BBHIBB")
SEQUENCE_MODULUS = 1 << 16

SCHEMA = """
CREATE TABLE IF NOT EXISTS definition (
    id            INTEGER PRIMARY KEY,
    digest        TEXT    NOT NULL UNIQUE,
    document      TEXT    NOT NULL,
    first_seen_ms INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS sample (
    id           INTEGER PRIMARY KEY,
    node         INTEGER NOT NULL,
    report       INTEGER NOT NULL,
    sequence     INTEGER NOT NULL,
    node_seconds INTEGER NOT NULL,
    clock_set    INTEGER NOT NULL,
    incomplete   INTEGER NOT NULL,
    value_count  INTEGER NOT NULL,
    received_ms  INTEGER NOT NULL,
    definition   INTEGER REFERENCES definition(id),
    frame        BLOB    NOT NULL
);

CREATE INDEX IF NOT EXISTS sample_series ON sample (node, report, id);

/*
 * What was not heard. The sequence is sixteen bits, so the wrap from 65535 to 0
 * is one step and not a gap of sixty-five thousand.
 */
CREATE VIEW IF NOT EXISTS gap AS
SELECT node,
       report,
       sequence AS after_sequence,
       next_sequence AS before_sequence,
       (next_sequence - sequence - 1 + 65536) % 65536 AS missing
FROM (SELECT node,
             report,
             sequence,
             LEAD(sequence) OVER (PARTITION BY node, report ORDER BY id)
                 AS next_sequence
      FROM sample)
WHERE next_sequence IS NOT NULL
  AND (next_sequence - sequence - 1 + 65536) % 65536 <> 0;
"""


class Store:
    """Writes what came down. Reading it back is sqlite3's job, not ours."""

    def __init__(self, path, document=None, now_ms=0):
        self.connection = sqlite3.connect(path)
        self.connection.executescript(SCHEMA)
        self.definition = self._definition(document, now_ms) if document else None

    def _definition(self, document, now_ms):
        digest = hashlib.sha256(document.encode()).hexdigest()
        cursor = self.connection.execute(
            "SELECT id FROM definition WHERE digest = ?", (digest,)
        )
        row = cursor.fetchone()
        if row:
            return row[0]
        cursor = self.connection.execute(
            "INSERT INTO definition (digest, document, first_seen_ms) VALUES (?, ?, ?)",
            (digest, document, now_ms),
        )
        self.connection.commit()
        return cursor.lastrowid

    def record(self, node, sample, received_ms):
        """Keep one reception. Committed before returning: a pass that ends in a
        crash still leaves what it heard."""
        _version, report, sequence, seconds, count, flags = SAMPLE_HEADER.unpack_from(sample)
        self.connection.execute(
            "INSERT INTO sample (node, report, sequence, node_seconds, clock_set,"
            " incomplete, value_count, received_ms, definition, frame)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (
                node,
                report,
                sequence,
                seconds,
                0 if flags & 2 else 1,
                1 if flags & 1 else 0,
                count,
                received_ms,
                self.definition,
                sample,
            ),
        )
        self.connection.commit()

    def close(self):
        self.connection.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
