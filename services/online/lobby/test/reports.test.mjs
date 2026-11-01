import assert from 'node:assert/strict';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createHmac, randomBytes } from 'node:crypto';
import test from 'node:test';
import {
  cleanupReportData,
  endReportRoster,
  readPlayerReport,
  readPlayerReports,
  REPORT_TTL_S,
  submitPlayerReport,
} from '../src/reports.mjs';
import { createNodeReportStore } from '../src/report-store-node.mjs';

const installA = 'a'.repeat(32);
const installB = 'b'.repeat(32);
const gameId = '0123456789abcdef';
const account = { id: 'guest:a', name: 'Alice', install: installA };
const now = 1_700_000_000_000;

test('Node journal replay repairs legacy report, roster and evidence strings before storage validation', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'openbfme-reports-text-'));
  try {
    const path = join(directory, 'journal.ndjson');
    const id = `R-${'a'.repeat(24)}`;
    const seconds = Math.floor(now / 1000);
    for (const text of ['\ud800x', 'é e\u0301 🌍']) {
      const report = { id, source: 'player', reporter: { install: installA, nick: text },
        target: { install: installB, nick: text }, game_id: gameId, reason: `other: ${text}`,
        received_at: seconds, expires_at: seconds + REPORT_TTL_S };
      const roster = { game_id: gameId, participants: [{ account: account.id, install: installA, nick: text }],
        active: [], state: 'finished', ended_at: seconds };
      const evidence = { expires_at: report.expires_at, lines: [{ game_id: gameId, nick: text, text, at: seconds }] };
      const journal = JSON.stringify({ v: 1, kind: 'snapshot', compacted_at: Date.now(), entries: [
        [`report:${id}`, report], [`roster:${gameId}`, roster], [`evidence:${id}`, evidence],
      ] }) + '\n';
      await writeFile(path, journal);
      const store = createNodeReportStore(directory);
      const response = await readPlayerReport({ store, now: () => now, reportId: id });
      assert.equal(response.report.reporter.nick, text.toWellFormed());
      assert.equal(response.report.target.nick, text.toWellFormed());
      assert.equal(response.report.reason, `other: ${text.toWellFormed()}`);
      assert.equal((await store.reportTransaction((tx) => tx.get(`roster:${gameId}`))).participants[0].nick, text.toWellFormed());
      const storedEvidence = await store.reportTransaction((tx) => tx.get(`evidence:${id}`));
      assert.equal(storedEvidence.lines[0].nick, text.toWellFormed());
      assert.equal(storedEvidence.lines[0].text, text.toWellFormed());
      if (text.isWellFormed()) assert.equal(JSON.stringify(response), JSON.stringify({ v: 1, report }));
      assert.equal(await readFile(path, 'utf8'), journal, 'read repair leaves the journal bytes alone');
    }
  } finally { await rm(directory, { recursive: true, force: true }); }
});
const roster = {
  id: gameId,
  slots: [
    { state: 'player', account: account.id, install: installA, name: 'Alice' },
    { state: 'player', account: 'guest:b', install: installB, name: 'Bob' },
  ],
  reportParticipants: [
    { account: account.id, install: installA, nick: 'Alice' },
    { account: 'guest:b', install: installB, nick: 'Bob' },
  ],
  state: 'started',
  finished: false,
};

function body(reason = 'other: unsporting play', target = 'guest:b') {
  return new TextEncoder().encode(JSON.stringify({ v: 1, game_id: gameId, target, reason }));
}

function liveGame(participants = roster.reportParticipants) {
  return { ...roster, reportParticipants: participants };
}

function hmac(key, data) {
  return new Uint8Array(createHmac('sha256', key).update(data).digest());
}

test('player report submission is private, idempotent, and survives restart', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'openbfme-reports-'));
  try {
    const first = createNodeReportStore(directory);
    await first.reportTransaction(async (tx) => tx.put(`roster:${gameId}`, {
      game_id: gameId,
      participants: roster.reportParticipants,
      active: [account.id, 'guest:b'],
      state: 'started',
      ended_at: null,
    }));
    const submit = await submitPlayerReport({
      store: first,
      now: () => now,
      randomBytes,
      account,
      rawBody: body(),
      idempotencyKey: 'same-report',
      getLiveGame: () => roster,
    });
    assert.match(submit.receipt.report_id, /^R-[0-9a-f]{24}$/);
    const replay = await submitPlayerReport({
      store: first,
      now: () => now + 1000,
      randomBytes,
      account,
      rawBody: body(),
      idempotencyKey: 'same-report',
      getLiveGame: () => roster,
    });
    assert.equal(replay.replay, true);
    assert.deepEqual(replay.receipt, submit.receipt);
    const attempts = await first.reportTransaction((tx) => tx.get(`attempts:${installA}`));
    assert.equal(attempts.length, 1, 'replay bypasses the attempt budget');

    const second = createNodeReportStore(directory);
    const detail = await readPlayerReport({ store: second, now: () => now, reportId: submit.receipt.report_id });
    const restartedReplay = await submitPlayerReport({
      store: second, now: () => now + 1000, randomBytes, account, rawBody: body(), idempotencyKey: 'same-report',
      getLiveGame: () => null,
    });
    assert.equal(restartedReplay.replay, true);
    assert.deepEqual(restartedReplay.receipt, submit.receipt);
    assert.equal((await second.reportTransaction((tx) => tx.get(`attempts:${installA}`))).length, 1);
    assert.equal((await second.reportTransaction((tx) => tx.get(`roster:${gameId}`))).game_id, gameId);
    assert.equal((await second.reportTransaction((tx) => tx.get('globalUTCday'))).count, 1);
    assert.deepEqual(Object.keys(detail.report).sort(), [
      'expires_at', 'game_id', 'id', 'reason', 'received_at', 'reporter', 'source', 'target',
    ]);
    assert.equal(detail.report.reporter.install, installA);
    assert.equal(detail.report.target.install, installB);
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});

test('report categories, roster names, and private bounded chat evidence are enforced', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'openbfme-reports-'));
  try {
    const store = createNodeReportStore(directory);
    const game = {
      ...roster,
      reportParticipants: [
        { account: account.id, install: installA, nick: 'Roster Alice' },
        { account: 'guest:b', install: installB, nick: 'Roster Bob' },
      ],
      chat: [
        { account: 'spectator', text: 'not part of pair', at: now },
        ...Array.from({ length: 60 }, (_, index) => ({
          account: index % 2 ? account.id : 'guest:b',
          text: `room line ${index}`,
          at: now + index * 1000,
        })),
      ],
    };
    await store.reportTransaction((tx) => tx.put(`roster:${gameId}`, {
      game_id: gameId, participants: game.reportParticipants, active: [account.id, 'guest:b'],
      state: 'started', ended_at: null,
    }));
    const submitted = await submitPlayerReport({
      store,
      now: () => now,
      randomBytes,
      account: { ...account, name: 'Untrusted client nickname' },
      rawBody: body('harassment: repeated abuse'),
      idempotencyKey: 'social-evidence-1',
      getLiveGame: () => game,
    });
    const evidence = await store.reportTransaction((tx) => tx.get(`evidence:${submitted.receipt.report_id}`));
    assert.equal(evidence.lines.length, 50);
    assert.ok(evidence.lines.every((line) => line.game_id === gameId && line.nick !== 'Untrusted client nickname'));
    assert.ok(evidence.lines.every((line) => Number.isSafeInteger(line.at)));
    const detail = await readPlayerReport({ store, now: () => now, reportId: submitted.receipt.report_id });
    assert.equal(detail.report.reporter.nick, 'Roster Alice');
    assert.equal(Object.hasOwn(detail.report, 'evidence'), false);
    await assert.rejects(() => submitPlayerReport({
      store,
      now: () => now + 1,
      randomBytes,
      account,
      rawBody: body('unsupported: note'),
      idempotencyKey: 'social-evidence-2',
      getLiveGame: () => game,
    }), (error) => error.code === 'bad_reason');
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});

test('report categories reject controls and malformed Unicode while legacy stored receipts remain replayable', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'openbfme-reports-reasons-'));
  try {
    const store = createNodeReportStore(directory);
    await store.reportTransaction((tx) => tx.put(`roster:${gameId}`, {
      game_id: gameId, participants: roster.reportParticipants, active: [account.id, 'guest:b'],
      state: 'started', ended_at: null,
    }));
    for (const reason of ['other: note\u0000hidden', 'harassment: \ud800']) {
      await assert.rejects(() => submitPlayerReport({
        store, now: () => now, randomBytes, account, rawBody: body(reason),
        idempotencyKey: `bad-${reason.includes('\u0000') ? 'control' : 'unicode'}-01`,
        getLiveGame: () => roster,
        // LOBBYSEC-1: text that is not well-formed Unicode is refused as bad_field before the category check, like every other lobby text
      }), (error) => error.code === (reason.includes('\u0000') ? 'bad_reason' : 'bad_field'));
    }
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});

test('Unicode private evidence reopens from the Node journal and expires after 90 days', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'openbfme-reports-unicode-'));
  try {
    const store = createNodeReportStore(directory);
    const game = {
      ...roster,
      reportParticipants: [
        { account: account.id, install: installA, nick: '😀'.repeat(24) },
        { account: 'guest:b', install: installB, nick: '旅行者'.repeat(8) },
      ],
      chat: [{ account: account.id, text: '🚀'.repeat(200), at: now }, { account: 'guest:b', text: 'é'.repeat(100), at: null }],
    };
    await store.reportTransaction((tx) => tx.put(`roster:${gameId}`, {
      game_id: gameId, participants: game.reportParticipants, active: [account.id, 'guest:b'],
      state: 'started', ended_at: null,
    }));
    const submitted = await submitPlayerReport({
      store, now: () => now, randomBytes, account, rawBody: body('other: Unicode evidence'),
      idempotencyKey: 'unicode-evidence-01', getLiveGame: () => game,
    });
    const reopened = createNodeReportStore(directory);
    const evidence = await reopened.reportTransaction((tx) => tx.get(`evidence:${submitted.receipt.report_id}`));
    assert.equal(evidence.lines.length, 2);
    assert.equal(evidence.lines[0].nick, '😀'.repeat(24));
    assert.equal(evidence.lines[0].text, '🚀'.repeat(200));
    assert.equal(evidence.lines[1].at, null);
    await cleanupReportData(reopened, Math.floor(now / 1000) + 90 * 24 * 60 * 60 + 1);
    assert.equal(await reopened.reportTransaction((tx) => tx.get(`evidence:${submitted.receipt.report_id}`)), null);
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});

test('strict invalid reports consume attempts and admin pagination is sparse', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'openbfme-reports-'));
  try {
    const store = createNodeReportStore(directory);
    await store.reportTransaction((tx) => tx.put(`roster:${gameId}`, {
      game_id: gameId, participants: roster.reportParticipants, active: [account.id, 'guest:b'],
      state: 'started', ended_at: null,
    }));
    await assert.rejects(() => submitPlayerReport({
      store, now: () => now, randomBytes, account,
      rawBody: new TextEncoder().encode('{"v":1,"game_id":"bad"}'),
      idempotencyKey: 'invalid-key', getLiveGame: () => roster,
    }), (error) => error.code === 'missing_field');
    const submit = await submitPlayerReport({
      store, now: () => now, randomBytes, account, rawBody: body(), idempotencyKey: 'valid-key',
      getLiveGame: () => roster,
    });
    const key = new Uint8Array(32).fill(7);
    const page = await readPlayerReports({ store, now: () => now, hmac, modKey: key, limit: 1 });
    assert.equal(page.items.length, 1);
    assert.equal(page.items[0].id, submit.receipt.report_id);
    assert.ok(page.next_cursor);
    await endReportRoster(store, gameId, Math.floor(now / 1000) - 90 * 24 * 60 * 60 - 1);
    await assert.rejects(() => readPlayerReport({ store, now: () => now + 90 * 24 * 60 * 60 * 1000, reportId: submit.receipt.report_id }), (error) => error.status === 404);
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});

function memoryReportStore({ failCommit = () => false } = {}) {
  const data = new Map();
  let queue = Promise.resolve();
  return {
    data,
    reportTransaction(fn) {
      const run = queue.then(async () => {
        const draft = new Map([...data].map(([key, value]) => [key, structuredClone(value)]));
        const tx = {
          get: async (key) => draft.has(key) ? structuredClone(draft.get(key)) : null,
          put: async (key, value) => { draft.set(key, structuredClone(value)); },
          delete: async (key) => { draft.delete(key); },
          list: async (prefix, { after = null, limit = 100, reverse = false } = {}) => {
            let keys = [...draft.keys()].filter((key) => key.startsWith(prefix));
            keys.sort();
            if (after !== null) keys = keys.filter((key) => reverse ? key < after : key > after);
            if (reverse) keys.reverse();
            return new Map(keys.slice(0, limit).map((key) => [key, structuredClone(draft.get(key))]));
          },
        };
        const result = await fn(tx);
        if (failCommit()) throw new Error('simulated transaction commit failure');
        data.clear();
        for (const [key, value] of draft) data.set(key, value);
        return result;
      });
      queue = run.catch(() => {});
      return run;
    },
  };
}

function testRoster(participants = roster.reportParticipants, endedAt = null) {
  return {
    game_id: gameId,
    participants,
    active: participants.map((participant) => participant.account),
    state: endedAt === null ? 'started' : 'finished',
    ended_at: endedAt,
  };
}

test('attempt quotas charge invalid bodies and enforce rolling hour and day limits', async () => {
  const store = memoryReportStore();
  await store.reportTransaction((tx) => tx.put(`roster:${gameId}`, testRoster()));
  let clock = now;
  const invalid = new TextEncoder().encode('not-json');
  const submitInvalid = (index) => submitPlayerReport({
    store, now: () => clock, randomBytes, account, rawBody: invalid,
    idempotencyKey: `invalid-${String(index).padStart(3, '0')}`, getLiveGame: () => null,
  });
  for (let batch = 0; batch < 4; batch++) {
    if (batch) clock += 3601_000;
    for (let item = 0; item < 5; item++) {
      await assert.rejects(submitInvalid(batch * 5 + item), (error) => error.status === 400);
    }
    if (batch === 0) {
      await assert.rejects(submitInvalid(5), (error) => error.status === 429 && error.details.retry_after <= 3600);
    }
  }
  await assert.rejects(submitInvalid(20), (error) => error.status === 429 && error.details.retry_after > 0);
  assert.equal(store.data.get(`attempts:${installA}`).length, 20);
});

test('same-key mismatch, different-key duplicate, and failed transactions remain atomic', async () => {
  const store = memoryReportStore();
  await store.reportTransaction((tx) => tx.put(`roster:${gameId}`, testRoster()));
  const first = await submitPlayerReport({
    store, now: () => now, randomBytes, account, rawBody: body(), idempotencyKey: 'report-key-1',
    getLiveGame: () => liveGame(),
  });
  await assert.rejects(submitPlayerReport({
    store, now: () => now, randomBytes, account, rawBody: body('other: different reason'),
    idempotencyKey: 'report-key-1', getLiveGame: () => liveGame(),
  }), (error) => error.status === 409 && error.code === 'conflict');
  await assert.rejects(submitPlayerReport({
    store, now: () => now, randomBytes, account, rawBody: body(), idempotencyKey: 'report-key-2',
    getLiveGame: () => liveGame(),
  }), (error) => error.status === 409 && error.code === 'already_reported');
  assert.ok(store.data.has(`report:${first.receipt.report_id}`));

  let failCommit = false;
  const failing = memoryReportStore({ failCommit: () => failCommit });
  await failing.reportTransaction((tx) => tx.put(`roster:${gameId}`, testRoster()));
  failCommit = true;
  await assert.rejects(submitPlayerReport({
    store: failing, now: () => now, randomBytes, account, rawBody: body(), idempotencyKey: 'report-key-3',
    getLiveGame: () => liveGame(),
  }), /simulated transaction commit failure/);
  assert.deepEqual([...failing.data.keys()], [`roster:${gameId}`], 'failed admission activates no report, quota, receipt, index, or duplicate guard');
});

test('global daily admission serializes requests at the 999-report boundary', async () => {
  const store = memoryReportStore();
  const accountC = { id: 'guest:c', name: 'Carol', install: 'c'.repeat(32) };
  const participants = [
    ...roster.reportParticipants,
    { account: accountC.id, install: accountC.install, nick: accountC.name },
  ];
  await store.reportTransaction(async (tx) => {
    await tx.put(`roster:${gameId}`, testRoster(participants));
    await tx.put('globalUTCday', { day: '2023-11-14', count: 999 });
  });
  const send = (who, key) => submitPlayerReport({
    store, now: () => now, randomBytes, account: who, rawBody: body(), idempotencyKey: key,
    getLiveGame: () => liveGame(participants),
  });
  const outcomes = await Promise.allSettled([
    send(account, 'global-report-a'),
    send(accountC, 'global-report-c'),
  ]);
  assert.equal(outcomes.filter((outcome) => outcome.status === 'fulfilled').length, 1);
  assert.equal(outcomes.filter((outcome) => outcome.status === 'rejected' && outcome.reason.code === 'daily_limit').length, 1);
  assert.equal(store.data.get('globalUTCday').count, 1000);
});

test('Node journal serializes concurrent global admissions at the 999 boundary', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'openbfme-reports-concurrent-'));
  try {
    const store = createNodeReportStore(directory);
    const accountC = { id: 'guest:c', name: 'Carol', install: 'c'.repeat(32) };
    const participants = [
      ...roster.reportParticipants,
      { account: accountC.id, install: accountC.install, nick: accountC.name },
    ];
    await store.reportTransaction(async (tx) => {
      await tx.put(`roster:${gameId}`, testRoster(participants));
      await tx.put('globalUTCday', { day: new Date(now).toISOString().slice(0, 10), count: 999 });
    });
    const send = (who, key) => submitPlayerReport({
      store, now: () => now, randomBytes, account: who, rawBody: body(), idempotencyKey: key,
      getLiveGame: () => liveGame(participants),
    });
    const outcomes = await Promise.allSettled([
      send(account, 'node-global-a'),
      send(accountC, 'node-global-c'),
    ]);
    assert.equal(outcomes.filter((outcome) => outcome.status === 'fulfilled').length, 1);
    assert.equal(outcomes.filter((outcome) => outcome.status === 'rejected' && outcome.reason.code === 'daily_limit').length, 1);
    assert.equal((await store.reportTransaction((tx) => tx.get('globalUTCday'))).count, 1000);
    assert.equal((await store.reportTransaction((tx) => tx.list('report:', { limit: 10 }))).size, 1);
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});

test('ended roster admits reports through 24 hours without resetting its end time', async () => {
  const store = memoryReportStore();
  const endedAt = Math.floor(now / 1000) - 60;
  await store.reportTransaction((tx) => tx.put(`roster:${gameId}`, testRoster(roster.reportParticipants, endedAt)));
  const result = await submitPlayerReport({
    store, now: () => now, randomBytes, account, rawBody: body(), idempotencyKey: 'after-game-1',
    getLiveGame: () => null,
  });
  assert.ok(result.receipt.report_id);
  await endReportRoster(store, gameId, endedAt + 1000);
  assert.equal(store.data.get(`roster:${gameId}`).ended_at, endedAt);
  await assert.rejects(submitPlayerReport({
    store, now: () => (endedAt + 24 * 60 * 60 + 1) * 1000, randomBytes, account, rawBody: body(),
    idempotencyKey: 'after-game-2', getLiveGame: () => null,
  }), (error) => error.status === 404);
});

test('Node journal compaction survives restart and attempt cleanup is bounded', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'openbfme-reports-'));
  try {
    const store = createNodeReportStore(directory);
    const oldInstalls = Array.from({ length: 150 }, (_, index) => index.toString(16).padStart(32, '0'));
    const freshInstall = 'f'.repeat(32);
    await store.reportTransaction(async (tx) => {
      for (const install of oldInstalls) await tx.put(`attempts:${install}`, [Math.floor(now / 1000) - 90000]);
      await tx.put(`attempts:${freshInstall}`, [Math.floor(now / 1000) - 30]);
    });
    let cursor = { index: 3, after: null };
    cursor = await cleanupReportData(store, Math.floor(now / 1000), cursor);
    assert.equal([...store.data.keys()].filter((key) => key.startsWith('attempts:')).length, 51);
    cursor = await cleanupReportData(store, Math.floor(now / 1000), cursor);
    assert.equal([...store.data.keys()].filter((key) => key.startsWith('attempts:')).length, 1);
    assert.ok(store.data.has(`attempts:${freshInstall}`));

    store.lastCompaction = 0;
    await store.reportTransaction((tx) => tx.get(`attempts:${freshInstall}`));
    const restarted = createNodeReportStore(directory);
    assert.ok(restarted.lastCompaction > 0, 'compaction timestamp comes from the durable snapshot');
    assert.deepEqual(await restarted.reportTransaction((tx) => tx.get(`attempts:${freshInstall}`)), [Math.floor(now / 1000) - 30]);
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});

test('Node journal compaction removes deleted report reasons from disk', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'openbfme-reports-compaction-'));
  try {
    const store = createNodeReportStore(directory);
    await store.reportTransaction((tx) => tx.put(`roster:${gameId}`, testRoster()));
    const reason = 'other: private reason removed by compaction';
    const submission = await submitPlayerReport({
      store, now: () => now, randomBytes, account, rawBody: body(reason), idempotencyKey: 'compact-reason-01',
      getLiveGame: () => liveGame(),
    });
    const receiptKey = `receipt:${installA}:compact-reason-01`;
    const duplicateKey = `duplicate:${installA}:${gameId}:${installB}`;
    const indexKey = `index:${String(submission.receipt.received_at).padStart(16, '0')}:${submission.receipt.report_id}`;
    await store.reportTransaction(async (tx) => {
      await tx.delete(`report:${submission.receipt.report_id}`);
      await tx.delete(receiptKey);
      await tx.delete(duplicateKey);
      await tx.delete(indexKey);
    });
    store.lastCompaction = 0;
    await store.reportTransaction((tx) => tx.get(`roster:${gameId}`));
    const journal = await readFile(join(directory, 'journal.ndjson'), 'utf8');
    assert.ok(!journal.includes(reason));
    const restarted = createNodeReportStore(directory);
    assert.equal(await restarted.reportTransaction((tx) => tx.get(`report:${submission.receipt.report_id}`)), null);
    assert.equal(await restarted.reportTransaction((tx) => tx.get(receiptKey)), null);
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});

test('Node journal rejects malformed and incomplete replay records', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'openbfme-reports-corrupt-'));
  try {
    await writeFile(join(directory, 'journal.ndjson'), `${JSON.stringify({
      v: 1,
      kind: 'transaction',
      ops: [['put', `report:R-${'a'.repeat(24)}`, { source: 'player' }]],
    })}\n`);
    assert.throws(() => createNodeReportStore(directory), /malformed value for report:/);
  } finally {
    await rm(directory, { recursive: true, force: true });
  }

  // DEVINPR-1 (review of WP-C): an interrupted append (no final newline) is cut off at startup; the complete records before it are kept
  const incomplete = await mkdtemp(join(tmpdir(), 'openbfme-reports-incomplete-'));
  try {
    const kept = `${JSON.stringify({ v: 1, kind: 'transaction', ops: [['put', 'globalUTCday', { day: '2026-10-19', count: 1 }]] })}\n`;
    await writeFile(join(incomplete, 'journal.ndjson'), `${kept}{"v":1,"kind":"transaction"`);
    const store = createNodeReportStore(incomplete);
    assert.deepEqual([...store.data.keys()], ['globalUTCday']);
    assert.equal(await readFile(join(incomplete, 'journal.ndjson'), 'utf8'), kept);
    await writeFile(join(incomplete, 'journal.ndjson'), '{"v":1,"kind":"transaction"');
    assert.equal(createNodeReportStore(incomplete).data.size, 0);
    assert.equal(await readFile(join(incomplete, 'journal.ndjson'), 'utf8'), '');
  } finally {
    await rm(incomplete, { recursive: true, force: true });
  }
});

test('Node journal: a failed append fails its transaction only; the store stays failed only when the journal cannot be cut back (DEVINPR-1)', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'openbfme-reports-append-'));
  try {
    const store = createNodeReportStore(directory);
    store.lastCompaction = Date.now();
    const journal = store.journalPath;
    store.journalPath = directory; // a directory: the append cannot even open it (nothing was written)
    await assert.rejects(store.reportTransaction((tx) => tx.put('globalUTCday', { day: '2026-10-19', count: 1 })));
    assert.equal(store.failed, null);
    store.journalPath = journal;
    await store.reportTransaction((tx) => tx.put('globalUTCday', { day: '2026-10-19', count: 2 }));
    assert.deepEqual(createNodeReportStore(directory).data.get('globalUTCday'), { day: '2026-10-19', count: 2 });
    if (process.platform === 'linux') {
      store.journalPath = '/dev/full'; // the write fails (ENOSPC) and a character device cannot be truncated: the store must refuse from now on
      await assert.rejects(store.reportTransaction((tx) => tx.put('globalUTCday', { day: '2026-10-19', count: 3 })));
      assert.notEqual(store.failed, null);
      store.journalPath = journal;
      await assert.rejects(store.reportTransaction((tx) => tx.get('globalUTCday')));
    }
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});

// LOBBYSEC-1: a report whose reason or target is not well-formed Unicode (a lone UTF-16 surrogate) is refused at intake (400), before any storage
test('LOBBYSEC-1: a report reason or target with a lone surrogate is refused (400)', async () => {
  for (const raw of [body('\ud800 cheat'), body('unsporting play', 'guest:\udc00')]) {
    await assert.rejects(submitPlayerReport({ store: memoryReportStore(), now: () => now, randomBytes, account, rawBody: raw, idempotencyKey: 'lobbysec-1',
      getLiveGame: () => roster }), (e) => e.status === 400 && e.code === 'bad_field');
  }
});

// LOBBYSEC-1 (Sol): ported from the live lobby line's test
test('a stored receipt from before the well-formed rule still validates; new intake refuses it (LOBBYSEC-1, as the live lobby line)', async () => {
  const { validReceipt } = await import('../src/reports.mjs');
  const canonical = JSON.stringify({ v: 1, game_id: gameId, target: 'guest:b', reason: 'legacy \ud800' });
  const receipt = { canonical, report_id: `R-${'1'.repeat(24)}`, received_at: 100, expires_at: 100 + 24 * 60 * 60 };
  assert.equal(validReceipt(receipt), receipt);
});
