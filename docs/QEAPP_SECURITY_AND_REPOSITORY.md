# QEAPP Security, Updates and Repository Design

Status: design baseline for the next implementation phase. This document does
not claim that permission revocation, Ed25519 verification or network downloads
are already implemented.

## 1. Permission Manager

Separate immutable package declarations from user grants:

- `requested_permissions`: parsed from signed manifest; never user-editable.
- `granted_permissions`: persisted per `manifest.id`; user-controlled subset
  of requested permissions.
- `denied_permissions`: derived as requested AND NOT granted.
- Unknown permission identifiers fail closed and are shown as unsupported.
- A package update may add requested permissions but must not silently grant
  them. Existing grants are intersected with the new request mask.
- Uninstall removes grants and app data. Reinstall starts with no optional
  grants unless the user explicitly restores a trusted backup.

Permission persistence must be a distinct field in a versioned appstore record;
do not overload the QEAPP's user data blob. Migrate appstore v3 records by
setting grants to the safe baseline, then commit v4 into the alternate slot.

Enforcement requirements:

- API calls check the effective grant mask at every entry point.
- Storage APIs resolve paths under the app's private directory and reject
  traversal, symlinks and cross-app paths.
- Native `.qen` code must not execute as UID 0. Until UID separation and
  syscall/filesystem isolation are enforced, native apps remain trusted code.
- Permission UI must label any capability that is not enforced as unavailable;
  a visual toggle alone is not a security boundary.

## 2. Update Manager

- Candidate source: local QEAFRESCUE update directory first; repository downloads
  are disabled until verified HTTPS/TLS and atomic file storage exist.
- Match packages by `manifest.id`, never by display name or filename.
- Parse versions as SemVer (major.minor.patch plus optional prerelease).
- Reject invalid versions and downgrade attempts by default; provide a separate
  explicit rollback action for signed, trusted packages.
- Verify the complete package signature and manifest before showing the update.
- Show installed/candidate version, publisher, new permissions and package size.
- On commit, stage the candidate, verify it again, atomically replace the catalog
  record, preserve app data and existing grants that remain requested.
- Keep the previous catalog slot intact for power-loss recovery.

## 3. Signed repository catalog (QEAF Repository v1)

Repository metadata is canonical UTF-8 JSON. The signature is detached and
covers the exact canonical bytes; the signature envelope identifies the key.
Never accept a catalog merely because it was fetched over the network.

Required catalog fields:

```json
{
  "format": "qeaf-repository",
  "format_version": 1,
  "repository_id": "org.qeafivels.official",
  "generated_at": "2026-11-20T00:00:00Z",
  "expires_at": "2026-11-27T00:00:00Z",
  "packages": [
    {
      "id": "org.example.notes",
      "name": "Notes",
      "version": "1.2.0",
      "publisher_id": "org.example",
      "channel": "stable",
      "architecture": ["arm64"],
      "min_os": "0.2.0",
      "size": 123456,
      "sha256": "<64 lowercase hex characters>",
      "package_url": "https://repo.example/apps/notes-1.2.0.qeapp",
      "permissions": ["ui.display", "storage.write.app"]
    }
  ]
}
```

The detached envelope contains `algorithm: Ed25519`, `key_id`, and base64
`signature`. The device pins repository public keys in the OS trust store.
Catalog expiry, repository identity, key revocation and rollback policy are
checked before package details are trusted. The package itself is independently
verified after download; catalog signature and package signature are separate.

Network implementation is gated on a native TLS backend, certificate
validation, trusted time/expiry policy, bounded downloads, SHA-256 verification
and atomic staging. Do not ship an HTTP-only fallback.

## 4. Publisher Trust and QEAPP signature migration

Production profile:

- Ed25519 signatures over a canonical package payload (all ZIP entries except
  the detached signature entry, sorted by normalized UTF-8 path).
- Private publisher keys stay on the developer's host or offline signing
  device; no private key or HMAC secret is embedded in QEAFIVELS OS.
- Device stores only trusted public keys and key IDs.
- Trust policy supports official, user-approved and revoked publishers.
- Unknown publisher, revoked key, malformed signature and unsupported
  algorithm fail closed.
- Existing HMAC-signed packages are marked `development/untrusted`; they may
  be accepted only in an explicitly enabled developer mode during migration.

Migration must be format-versioned. Do not silently reinterpret the existing
v1 HMAC `SIGNATURE` field as Ed25519. Keep legacy verification isolated, add
Ed25519 test vectors and host/device cross-verification, then switch release
packages only after both sides agree on canonicalization.

## 5. Current implementation boundary

The current Violet implementation parses requested permission bits and checks
some framework API calls against the manifest mask. It does not yet persist
user grants separately. Native children still share UID 0, so direct syscalls
can bypass the framework API. Current package signatures use an embedded
HMAC-SHA256 development key. The repository network/TLS path is not active.
These limitations are security blockers for third-party distribution.

## 6. Implementation order

1. Add appstore v4 permission-grant records with v2/v3 backward migration.
2. Add Permission Manager UI and API enforcement; deny unimplemented APIs.
3. Add strict SemVer/update comparison and atomic update/rollback tests.
4. Add Ed25519 host signer/verifier and on-device public-key verification.
5. Add signed repository catalog parser and offline fixtures/tests.
6. Add native TLS/download backend only after certificate and time validation
   are available in the standalone OS.



## Implementation status — 2026-11-20

### Completed in this pass: appstore v4 grant persistence foundation

- Catalog writer now emits `QEAFAPP4` / version 4 records.
- v4 record layout: name length, package length, app-data length, 32-bit
  granted-permission mask, package name, package bytes, app-data bytes.
- Reader remains backward-compatible with v2 (8-byte record header) and v3
  (12-byte record header). Legacy records expose zero optional grants; the next
  successful save writes the v4 layout to the alternate slot.
- Installed app state now carries `granted_permissions` separately from the
  immutable manifest-requested `permissions` mask.
- Persistence writes the grant mask; update path intersects old grants with
  the new manifest request and never silently grants newly requested bits.
- UI display is retained as the minimal baseline capability; optional
  capabilities default to denied.

### Verified

- `python scripts/build_violet.py`: PASS, linked `vqeaf-init` (9,650,768
  bytes, 265 objects).
- `python -m unittest tests.test_qeapp tests.test_qeapp_native -v`: PASS,
  63 tests.

### Not yet completed / deliberately gated

- Permission Manager screen and interactive grant/revoke controls are not yet
  implemented. The current device runtime still derives its effective mask
  from manifest permissions, so v4 persistence must not be mistaken for
  runtime enforcement.
- Native QEAPP child still shares UID 0 and can bypass framework permission
  checks with direct syscalls. Do not enable untrusted native packages until
  privilege separation and syscall/filesystem isolation are in place.
- Update discovery, strict SemVer, staged replacement and rollback are not
  implemented in this pass.
- Ed25519 signer/verifier and publisher trust are not implemented. Existing
  HMAC development format remains unchanged.
- Signed repository parser has not been implemented; network access remains
  disabled. Native TLS and downloads remain gated on a verified TLS backend,
  certificate validation and trusted time.

 
## Permission Manager implementation — 2026-11-20

- App detail page now includes a Vietnamese Permission Manager section.
- Each requested capability has a grant/revoke action. The UI does not offer
  capabilities absent from the signed manifest.
- \`vqeaf_qeapp_set_permission()\` validates a single known permission bit,
  checks it is requested, changes the per-app grant mask and persists via
  appstore v4. \`vqeaf_qeapp_effective_permissions()\` returns the intersection
  of requested and granted masks.
- Fresh installs and legacy v2/v3 migrations start with zero optional grants.
  Updates intersect old grants with the new request set; new permissions are
  never silently enabled.
- Native execution is now fail-closed: launch returns an explicit sandbox
  unavailable error before fork/module execution. The UI marks native apps as
  temporarily blocked. This prevents a false security claim while child
  processes still inherit the shell UID.
- Existing framework API permission checks still need to be wired to the
  effective grant mask when native sandboxing is implemented. Native direct
  syscalls remain an untrusted-code boundary; no native app is executed.
- Build: \`python scripts/build_violet.py\` PASS (9,614,712 bytes, 265 objects).
- Tests: \`python -m unittest tests.test_qeapp tests.test_qeapp_native -v\`
  PASS (63 tests). These existing tests do not exercise device UI interaction,
  appstore v4 grant persistence on real p77 media, or privilege separation.
