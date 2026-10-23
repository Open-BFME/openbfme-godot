// Lane ONLINEMENU-1: the second player of the FRIENDS page video (tools/net/onlinemenu1_friends_video.sh). Signs in as "Elrond" on a local lobby,
// writes its friend code to a file, accepts the first friend request it gets, then writes one chat line to that friend and stays online.
// node onlinemenu1_friend_helper.mjs <lobby url> <code file>
import { writeFileSync } from 'node:fs';
const [, , base, codeFile] = process.argv;
async function call(method, path, token, body) {
  const r = await fetch(base + path, { method, headers: { 'content-type': 'application/json', ...(token ? { authorization: `Bearer ${token}` } : {}) },
    body: body === undefined ? undefined : JSON.stringify(body) });
  const text = await r.text();
  try { return { status: r.status, body: JSON.parse(text) }; } catch { return { status: r.status, body: text }; }
}
const install = 'e1'.repeat(16);
const s = await call('POST', '/v1/auth/guest', null, { v: 1, name: 'Elrond', install, client: { platform: 'linux', version: 'video' } });
if (s.status !== 200) { console.error('sign-in', s.status, JSON.stringify(s.body)); process.exit(1); }
const token = s.body.token;
const me = await call('GET', '/v1/me', token);
writeFileSync(codeFile, String(me.body.code));
console.log('helper code', me.body.code);
let seq = '';
let friend = null;
const until = Date.now() + 300000;
while (Date.now() < until) {
  const inbox = await call('GET', '/v1/inbox' + (seq ? `?since=${encodeURIComponent(seq)}&wait=5` : '?wait=5'), token);
  if (inbox.status === 200) {
    seq = inbox.body.seq ?? seq;
    for (const req of inbox.body.requests ?? []) {
      if (req.to?.pid === me.body.pid && !friend) {
        await new Promise((r) => setTimeout(r, 3000)); // a moment for the video
        const a = await call('POST', `/v1/friends/requests/${req.id}/accept`, token, { v: 1 });
        console.log('accept', a.status);
        friend = req.from.pid;
        await new Promise((r) => setTimeout(r, 4000));
        const m = await call('POST', `/v1/chat/${friend}/messages`, token, { v: 1, client_id: 'ab'.repeat(16), text: 'Good game earlier! Up for another one?' });
        console.log('chat', m.status);
      }
    }
  } else {
    await new Promise((r) => setTimeout(r, 1000));
  }
}
