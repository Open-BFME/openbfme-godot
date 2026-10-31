import test from 'node:test';
import assert from 'node:assert/strict';
import { repairStoredText } from '../src/stored-text.mjs';

test('stored text repair is copy-on-change, recursive and idempotent', () => {
  const good = { name: 'é e\u0301 🌍 literal \\ud800x', rows: [null, 1, false, { text: 'unchanged' }] };
  assert.equal(repairStoredText(good), good);
  const raw = { good, rows: [{ text: '\ud800x' }, '\udc00x', '\ud800\udc00'] };
  const clean = repairStoredText(raw);
  assert.deepEqual(clean.rows, [{ text: '�x' }, '�x', '\ud800\udc00']);
  assert.equal(clean.good, good);
  assert.equal(raw.rows[0].text, '\ud800x');
  assert.equal(repairStoredText(clean), clean);
  const keyed = { '\ud800x': good };
  const repairedKey = repairStoredText(keyed);
  assert.deepEqual(repairedKey, { '�x': good });
  assert.equal(repairedKey['�x'], good);
  assert.equal(repairStoredText(repairedKey), repairedKey);
  assert.equal(Object.keys(keyed)[0], '\ud800x');
});
