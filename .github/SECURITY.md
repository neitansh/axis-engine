# Security Policy

## Scope

This policy covers **Axis**, the engine in this repository.

Axis is a fork of [Luanti](https://github.com/luanti-org/luanti) and shares most
of its code with it. That matters for where a report should go:

- A vulnerability in code Axis wrote or changed → report it here.
- A vulnerability that also exists in unmodified upstream code → please report
  it to Luanti as well, following
  [their security policy](https://github.com/luanti-org/luanti/blob/master/.github/SECURITY.md).
  Fixing it upstream fixes it for everyone, not only for Axis.

If you cannot tell which of the two it is, report it here and say so; sorting
that out is our job, not yours.

## Supported versions

Only the latest state of the `main` branch is supported. Axis has no released
versions to backport fixes to yet.

## Reporting a vulnerability

Please report privately, so that a fix can exist before the problem is public.

Use GitHub's private vulnerability reporting on this repository: the
**Security** tab → **Report a vulnerability**. If that is not available to you,
open an issue that says only that you have found a security problem and asks for
a private channel — without any details of the problem itself.

Please include, as far as you can:

- what the problem is and what an attacker can do with it,
- the steps to reproduce it,
- the version or commit you tested,
- whether you believe it affects upstream Luanti too.

For the reasoning behind private reporting, see
[Responsible Disclosure](https://en.wikipedia.org/wiki/Responsible_disclosure).
