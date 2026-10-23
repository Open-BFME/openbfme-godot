# OpenBFME mail function

ONLINEMENU-1 (owner identity decisions 2026-10-20; design in `docs/IDENTITY.md`, "Mail function").

An AWS Lambda that sends the lobby's e-mail sign-in links through Amazon SES. It runs on the standard Python 3.12 runtime and uses only the
boto3 that ships with it. The lobby (`services/online/lobby/src/email.mjs`, `sendMail`) POSTs `{v:1, to, subject, text, html}` to the function URL
with two headers:

- `x-openbfme-time`: unix seconds. A request more than 300 s off this function's clock is refused.
- `x-openbfme-sig`: hex HMAC-SHA256 with `MAILER_KEY` over `"<time>\n<raw body>"`.

The function checks the signature with a constant-time compare, refuses a signature it has already seen, checks the fields (one address of at most
254 characters, no CR or LF in the address or subject, subject at most 200 characters, text and HTML at most 32 KiB each, body at most 64 KiB) and
sends one message from `no-reply@openbfme.com`. It answers `200 {"ok":true}` or `{"ok":false,"error":"<code>"}`: 4xx for a refused request,
500 when it is not configured, 502 when SES refuses the send.

Its logs hold only the Lambda request id and an outcome code (`sent`, `bad_signature`, `send_failed:MessageRejected`, ...). The address, the body
and the key are never logged, and SES error messages (which can name the address) are reduced to their error code.

Tests: `python3 -m unittest services/online/mailer/test_handler.py` (pytest runs them too). They use a fake SES client, so boto3 is not needed locally.

## Configuration

| Variable | Value |
|---|---|
| `MAILER_KEY` | the shared secret, hex, at least 32 bytes (`openssl rand -hex 32`). The same value goes to the lobby Worker as its `MAILER_KEY` secret. |
| `MAIL_FROM` | `no-reply@openbfme.com` (the default). The IAM policy allows no other sender. |
| `SES_REGION` | optional, default `us-east-2` |

## Deploy (owner, or an agent with a short-lived `aws login` session)

There are no static AWS keys. Sign in with `aws login --region us-east-2` (browser sign-in, short-lived credentials). Replace `<ACCOUNT_ID>` with the
12-digit account id (`aws sts get-caller-identity --query Account --output text`).

1. **SES identity** (once). Create the domain identity and publish its three DKIM CNAMEs in Cloudflare DNS for openbfme.com:
   `aws sesv2 create-email-identity --email-identity openbfme.com`, then `aws sesv2 get-email-identity --email-identity openbfme.com` shows the
   DKIM tokens (`<token>._domainkey.openbfme.com CNAME <token>.dkim.amazonses.com`). A new account starts in the SES sandbox: 200 messages a day,
   and only to verified addresses. Request production access in the SES console before players use e-mail sign-in.
2. **Log group**, kept 14 days so logs cost nothing to speak of:
   ```
   aws logs create-log-group --log-group-name /aws/lambda/openbfme-mailer
   aws logs put-retention-policy --log-group-name /aws/lambda/openbfme-mailer --retention-in-days 14
   ```
3. **Role.** Put `<ACCOUNT_ID>` into `iam-policy.json` (a local copy), then:
   ```
   aws iam create-role --role-name openbfme-mailer \
     --assume-role-policy-document '{"Version":"2012-10-17","Statement":[{"Effect":"Allow","Principal":{"Service":"lambda.amazonaws.com"},"Action":"sts:AssumeRole"}]}'
   aws iam put-role-policy --role-name openbfme-mailer --policy-name openbfme-mailer --policy-document file://iam-policy.json
   ```
4. **Function** (from this directory):
   ```
   zip mailer.zip handler.py
   aws lambda create-function --function-name openbfme-mailer --runtime python3.12 --handler handler.lambda_handler \
     --role arn:aws:iam::<ACCOUNT_ID>:role/openbfme-mailer --zip-file fileb://mailer.zip --timeout 10 --memory-size 128 \
     --environment "Variables={MAILER_KEY=<hex secret>,MAIL_FROM=no-reply@openbfme.com}"
   aws lambda put-function-concurrency --function-name openbfme-mailer --reserved-concurrent-executions 2
   ```
   Later code updates: `aws lambda update-function-code --function-name openbfme-mailer --zip-file fileb://mailer.zip`.
   Typing the key on the command line leaves it in the shell history: clear it afterwards, or pass `--environment file://env.json` and delete the file.
5. **Function URL**, auth type NONE (the HMAC is the authentication):
   ```
   aws lambda create-function-url-config --function-name openbfme-mailer --auth-type NONE
   aws lambda add-permission --function-name openbfme-mailer --statement-id public-url --action lambda:InvokeFunctionUrl \
     --principal '*' --function-url-auth-type NONE
   ```
   If the URL answers 403 Forbidden, the account also needs `lambda:InvokeFunction` for principal `*` limited to function-URL calls
   (AWS's newer rule for function URLs; the console's "Function URL" page adds both statements).
6. **Lobby secrets.** Give the Worker the function URL as `MAILER_URL` and the same hex secret as `MAILER_KEY` (Worker secrets, not plain vars).
7. **Check.** Send one link to a verified address from the lobby's request page and look for a `sent` line in the log group.

To rotate the key: generate a new one, set it on the function (`aws lambda update-function-configuration --environment ...`) and on the Worker
right after; sends in between fail and the lobby reports them as provider failures.

## Costs

- Lambda: the free tier (1 million requests and 400,000 GB-seconds a month) covers far more than the lobby's cap of 300 sends a day. Reserved
  concurrency 2 bounds how fast anyone can make it run.
- SES: $0.10 per 1,000 messages after its free allowance. 300 a day is about 9,000 a month, under $1.
- CloudWatch Logs: a few short lines per send, well inside the free 5 GB; the 14-day retention keeps storage near zero.
- The function URL itself costs nothing extra.

## Limits

- Replay protection is per container. Each warm container remembers the signatures it has accepted for 5 minutes (at most 4096, then it answers
  429 `busy`). A captured request replayed to a different container, or after a cold start, within the 5-minute window is sent again. A replay can
  only resend the same message to the same address, so the harm is one duplicate e-mail; a shared store (DynamoDB) would close this, at the cost of
  one more service.
- The function URL is public. Unsigned calls are refused before SES is touched, but they still count as Lambda invocations. Reserved concurrency 2
  limits the rate, and the free tier absorbs ordinary noise.
- New AWS accounts can have a total concurrency limit of 10 with 10 required to stay unreserved; then `put-function-concurrency` fails and needs a
  quota increase first (Service Quotas, "Concurrent executions").
- In the SES sandbox, SES checks the role's permission against the recipient's verified identity as well. Test sends to a verified address can
  fail with "not authorized" until production access is granted (or until a temporary statement for that identity is added).
- `MAILER_KEY` sits in the function's environment (encrypted at rest with the AWS-managed key). Anyone who can read the function configuration
  can read it. Secrets Manager would hide it from them but costs $0.40 a month per secret.
- Bounces and complaints use SES's account-level suppression list (on by default). There is no feedback handling beyond that.
- The address check here is basic syntax only. The lobby's `normalizeEmail` is the strict check.
