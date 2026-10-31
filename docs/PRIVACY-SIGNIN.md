# What OpenBFME online keeps about you

*Draft for the owner's review (lane ONLINEMENU-1). For openbfme.com/privacy, linked from the launcher's sign-in screen.*

You can play online without an account. To run online play, keep banned players out and let you move your player to
another computer, the OpenBFME lobby keeps a few things:

**Always, when you play online**
- A random id the game makes once on your computer, and the name you choose.
- Your operating system's name and the client version, when supplied by the game or launcher.
- A scrambled (one-way hashed) form of your computer's machine id, when the client can read it. We use it only to keep banned players out. It is never shown
  to other players, and it can't be turned back into the id.
- A recovery code, if you make one: only a hashed form of it.

**When you use the social features**
- Your friend code, friends, friend requests, blocks and their private notes, privacy settings and recent games.
- Friend chat messages and read markers. Friend chat history is kept permanently; removing a friend does not delete it.
- Presence and invitations while the lobby is running. Reports can retain server-held room chat as private evidence for moderation.

**Only if you choose to link them**
- **Discord:** a hashed form of your Discord user id. Not your name, picture, friends or messages.
- **An e-mail address:** a hashed fingerprint of the address, so we can recognise it when you sign in on another computer. We use
  the address once, to send you the sign-in link (through Amazon's e-mail service), and we don't keep it.

**Why:** to move your player and friends list to another computer, and to make bans stick (a ban covers the player, its linked
Discord account or e-mail address, and the machine id hash).

**Deleting it:** unlink Discord or your e-mail address in the launcher (Multiplayer) and its hash is removed from your player.
To have your whole player removed, write to admin@openbfme.com with your friend code. Hashes that belong to a ban are kept while
the ban lasts.

**Who handles it:** the lobby runs on Cloudflare; sign-in e-mails are sent by Amazon Web Services; Discord handles the Discord
sign-in itself.
