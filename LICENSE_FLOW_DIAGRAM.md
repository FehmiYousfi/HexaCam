# License System Flow Diagrams

## System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         HexaCam License System                   │
└─────────────────────────────────────────────────────────────────┘

┌──────────────────────┐                    ┌──────────────────────┐
│   LicenseForge       │                    │   CameraSoftware     │
│   (Electron App)     │                    │   (Qt/C++ App)       │
├──────────────────────┤                    ├──────────────────────┤
│ • Generate licenses  │                    │ • Validate licenses  │
│ • Get fingerprints   │                    │ • Check at startup   │
│ • Install locally    │                    │ • Show errors        │
│ • Install via SSH    │                    │ • Display fingerprint│
└──────────┬───────────┘                    └──────────┬───────────┘
           │                                           │
           │ Creates                                   │ Reads
           ▼                                           ▼
    ┌─────────────────────────────────────────────────────┐
    │              License File (.lic)                     │
    │  Format: LICF + Version + IV + AuthTag + Encrypted  │
    │  Locations:                                          │
    │    1. ~/.licenseforge/local_license.lic             │
    │    2. <app_dir>/license.lic                         │
    │    3. /opt/myapp/license.lic                        │
    └─────────────────────────────────────────────────────┘
```

## License Generation Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                    License Generation Process                    │
└─────────────────────────────────────────────────────────────────┘

User Input                    LicenseForge Processing
───────────                   ───────────────────────

┌──────────────┐
│ Get Machine  │
│ Fingerprint  │              ┌─────────────────────────┐
└──────┬───────┘              │ Read /sys/.../uuid or   │
       │                      │ /etc/machine-id         │
       └─────────────────────>│ Return: UUID string     │
                              └──────────┬──────────────┘
                                         │
┌──────────────┐                         │
│ Fill Details │                         │
│ • Name       │                         │
│ • Email      │                         │
│ • Expiry     │                         │
└──────┬───────┘                         │
       │                                 │
       │                                 │
┌──────▼───────┐                         │
│   Generate   │                         │
│   License    │                         │
└──────┬───────┘                         │
       │                                 │
       └─────────────────────────────────┤
                                         │
                              ┌──────────▼──────────────┐
                              │ 1. Create Payload       │
                              │    {fingerprint, name,  │
                              │     email, type, expiry}│
                              └──────────┬──────────────┘
                                         │
                              ┌──────────▼──────────────┐
                              │ 2. Sign with RSA-4096   │
                              │    Private Key          │
                              └──────────┬──────────────┘
                                         │
                              ┌──────────▼──────────────┐
                              │ 3. Combine Payload +    │
                              │    Signature            │
                              └──────────┬──────────────┘
                                         │
                              ┌──────────▼──────────────┐
                              │ 4. Derive AES Key from  │
                              │    Fingerprint (SHA256) │
                              └──────────┬──────────────┘
                                         │
                              ┌──────────▼──────────────┐
                              │ 5. Encrypt with         │
                              │    AES-256-GCM          │
                              └──────────┬──────────────┘
                                         │
                              ┌──────────▼──────────────┐
                              │ 6. Create Binary File   │
                              │    LICF + Ver + IV +    │
                              │    AuthTag + Ciphertext │
                              └──────────┬──────────────┘
                                         │
┌──────────────┐                         │
│   Install    │                         │
│   License    │<────────────────────────┘
└──────┬───────┘
       │
       ▼
┌──────────────────────────────┐
│ ~/.licenseforge/             │
│    local_license.lic         │
└──────────────────────────────┘
```

## License Validation Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                   License Validation Process                     │
└─────────────────────────────────────────────────────────────────┘

CameraSoftware Startup
──────────────────────

┌──────────────────┐
│ Application      │
│ Starts           │
└────────┬─────────┘
         │
         ▼
┌────────────────────────────────────────┐
│ Search for License File                │
│ 1. ~/.licenseforge/local_license.lic   │
│ 2. <app_dir>/license.lic               │
│ 3. /opt/myapp/license.lic              │
└────────┬───────────────────────────────┘
         │
         ├─── Not Found ──────────────────┐
         │                                │
         ▼                                │
┌────────────────────┐                    │
│ File Found?        │                    │
└────────┬───────────┘                    │
         │ Yes                            │
         ▼                                │
┌────────────────────────────────┐        │
│ Read Binary File               │        │
└────────┬───────────────────────┘        │
         │                                │
         ▼                                │
┌────────────────────────────────┐        │
│ Check Magic Bytes              │        │
│ Expected: "LICF" (0x4C494346)  │        │
└────────┬───────────────────────┘        │
         │                                │
         ├─── Invalid ────────────────────┤
         │                                │
         ▼                                │
┌────────────────────────────────┐        │
│ Check Version                  │        │
│ Expected: 0x01                 │        │
└────────┬───────────────────────┘        │
         │                                │
         ├─── Invalid ────────────────────┤
         │                                │
         ▼                                │
┌────────────────────────────────┐        │
│ Extract Components:            │        │
│ • IV (12 bytes)                │        │
│ • Auth Tag (16 bytes)          │        │
│ • Ciphertext (remaining)       │        │
└────────┬───────────────────────┘        │
         │                                │
         ▼                                │
┌────────────────────────────────┐        │
│ Get Machine Fingerprint        │        │
│ Read: /sys/.../uuid or         │        │
│       /etc/machine-id          │        │
└────────┬───────────────────────┘        │
         │                                │
         ▼                                │
┌────────────────────────────────┐        │
│ Derive AES Key                 │        │
│ SHA256(fingerprint)            │        │
└────────┬───────────────────────┘        │
         │                                │
         ▼                                │
┌────────────────────────────────┐        │
│ Decrypt with AES-256-GCM       │        │
│ Using: Key, IV, Auth Tag       │        │
└────────┬───────────────────────┘        │
         │                                │
         ├─── Decryption Failed ──────────┤
         │    (Wrong Machine)             │
         │                                │
         ▼                                │
┌────────────────────────────────┐        │
│ Parse JSON Container           │        │
│ Extract: data, signature       │        │
└────────┬───────────────────────┘        │
         │                                │
         ▼                                │
┌────────────────────────────────┐        │
│ Verify RSA Signature           │        │
│ Using: Public Key              │        │
└────────┬───────────────────────┘        │
         │                                │
         ├─── Signature Invalid ──────────┤
         │    (Tampered)                  │
         │                                │
         ▼                                │
┌────────────────────────────────┐        │
│ Parse Inner Payload            │        │
│ Extract: type, expiry, etc.    │        │
└────────┬───────────────────────┘        │
         │                                │
         ▼                                │
┌────────────────────────────────┐        │
│ Check License Type             │        │
│ Expected: "CameraSoftware"     │        │
└────────┬───────────────────────┘        │
         │                                │
         ├─── Wrong Type ─────────────────┤
         │                                │
         ▼                                │
┌────────────────────────────────┐        │
│ Check Expiry Date              │        │
│ If set: Compare with current   │        │
└────────┬───────────────────────┘        │
         │                                │
         ├─── Expired ────────────────────┤
         │                                │
         ▼                                │
┌────────────────────────────────┐        │
│ ✓ License Valid                │        │
│ Continue Startup               │        │
└────────────────────────────────┘        │
                                          │
                                          ▼
                              ┌───────────────────────┐
                              │ ✗ License Invalid     │
                              │ Show Error Dialog:    │
                              │ • Error message       │
                              │ • Machine fingerprint │
                              │ • Checked locations   │
                              │ Exit Application      │
                              └───────────────────────┘
```

## Data Flow Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                         Data Transformations                     │
└─────────────────────────────────────────────────────────────────┘

GENERATION (LicenseForge)
─────────────────────────

Customer Data                    Cryptographic Operations
─────────────                    ────────────────────────

{                                ┌──────────────────────┐
  fingerprint: "abc123",         │ JSON.stringify()     │
  customerName: "John",    ────> │                      │
  customerEmail: "j@e.com",      └──────┬───────────────┘
  licenseType: "CameraSoftware",        │
  expiry: "2025-12-31",                 │ payloadString
  issuedAt: "2024-01-01"                │
}                                       ▼
                              ┌──────────────────────┐
                              │ RSA Sign             │
private-key.pem ────────────> │ (SHA-256 + RSA-4096) │
                              └──────┬───────────────┘
                                     │ signature (base64)
                                     ▼
                              ┌──────────────────────┐
                              │ Combine:             │
                              │ {                    │
                              │   data: payload,     │
                              │   signature: sig     │
                              │ }                    │
                              └──────┬───────────────┘
                                     │ signedData (JSON)
                                     ▼
                              ┌──────────────────────┐
fingerprint ──> SHA256() ───> │ AES-256-GCM Encrypt  │
                              │ Key: SHA256(fp)      │
                              │ IV: random(12)       │
                              └──────┬───────────────┘
                                     │
                                     ├─> IV (12 bytes)
                                     ├─> AuthTag (16 bytes)
                                     └─> Ciphertext (N bytes)
                                     │
                                     ▼
                              ┌──────────────────────┐
                              │ Binary Pack:         │
                              │ "LICF" + 0x01 +      │
                              │ IV + AuthTag +       │
                              │ Ciphertext           │
                              └──────┬───────────────┘
                                     │
                                     ▼
                              license.lic (binary file)


VALIDATION (CameraSoftware)
───────────────────────────

Binary File                      Cryptographic Operations
───────────                      ────────────────────────

license.lic                       ┌──────────────────────┐
(binary)                          │ Read Binary          │
    │                             │ Parse Structure      │
    └────────────────────────────>│                      │
                                  └──────┬───────────────┘
                                         │
                                         ├─> Magic: "LICF"
                                         ├─> Version: 0x01
                                         ├─> IV (12 bytes)
                                         ├─> AuthTag (16 bytes)
                                         └─> Ciphertext
                                         │
                                         ▼
                              ┌──────────────────────┐
fingerprint ──> SHA256() ───> │ AES-256-GCM Decrypt  │
                              │ Key: SHA256(fp)      │
                              │ IV: from file        │
                              │ AuthTag: from file   │
                              └──────┬───────────────┘
                                     │ signedData (JSON)
                                     ▼
                              ┌──────────────────────┐
                              │ Parse JSON:          │
                              │ {                    │
                              │   data: payload,     │
                              │   signature: sig     │
                              │ }                    │
                              └──────┬───────────────┘
                                     │
                                     ├─> payloadString
                                     └─> signature (base64)
                                     │
                                     ▼
                              ┌──────────────────────┐
public-key (embedded) ──────> │ RSA Verify           │
payloadString ──────────────> │ (SHA-256 + RSA-4096) │
signature ───────────────────>│                      │
                              └──────┬───────────────┘
                                     │ valid? (bool)
                                     ▼
                              ┌──────────────────────┐
                              │ Parse Payload JSON   │
                              │ Extract fields       │
                              └──────┬───────────────┘
                                     │
                                     ▼
                              {
                                fingerprint: "abc123",
                                customerName: "John",
                                licenseType: "CameraSoftware",
                                expiry: "2025-12-31",
                                ...
                              }
                                     │
                                     ▼
                              ┌──────────────────────┐
                              │ Validate:            │
                              │ • Type matches       │
                              │ • Not expired        │
                              │ • Fingerprint matches│
                              └──────┬───────────────┘
                                     │
                                     ▼
                              ✓ Valid or ✗ Invalid
```

## Component Interaction

```
┌─────────────────────────────────────────────────────────────────┐
│                    Component Communication                       │
└─────────────────────────────────────────────────────────────────┘

LicenseForge (Electron)
───────────────────────

┌──────────────┐         IPC          ┌──────────────┐
│   Renderer   │ ◄─────────────────► │     Main     │
│   (React)    │                      │   (Node.js)  │
└──────────────┘                      └──────┬───────┘
      │                                      │
      │ User Actions                         │ File System
      │ • Click buttons                      │ • Read/Write
      │ • Fill forms                         │ • SSH
      │                                      │ • Crypto
      │                                      │
      └──────────────────────────────────────┘

IPC Methods:
• getLocalFingerprint()
• installLocalLicense(data)
• removeLocalLicense()
• checkLocalLicense()
• fetchFingerprint(sshConfig)
• generateLicense(licenseData)


CameraSoftware (Qt/C++)
───────────────────────

┌──────────────┐                      ┌──────────────┐
│    main()    │                      │   License    │
│              │ ◄─────────────────► │  Validator   │
│ • Startup    │   validate()         │              │
│ • UI Init    │   getMachineFingerprint() │ • Decrypt    │
└──────────────┘                      │ • Verify     │
                                      └──────┬───────┘
                                             │
                                             │ File System
                                             │ • Read license
                                             │ • Read fingerprint
                                             │
                                             ▼
                                      ┌──────────────┐
                                      │   OpenSSL    │
                                      │ • AES-GCM    │
                                      │ • RSA Verify │
                                      │ • SHA-256    │
                                      └──────────────┘
```

## Error Handling Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                        Error Scenarios                           │
└─────────────────────────────────────────────────────────────────┘

Validation Step              Success Path    Error Path
───────────────              ────────────    ──────────

File Exists? ────────────────┬─── Yes ──────> Continue
                             └─── No ───────> "License file not found"
                                              Show fingerprint
                                              Exit

Magic Bytes Valid? ──────────┬─── Yes ──────> Continue
                             └─── No ───────> "Invalid license format"
                                              Exit

Version Valid? ──────────────┬─── Yes ──────> Continue
                             └─── No ───────> "Invalid license format"
                                              Exit

AES Decrypt Success? ────────┬─── Yes ──────> Continue
                             └─── No ───────> "Hardware binding failed"
                                              "Wrong machine or tampered"
                                              Exit

RSA Verify Success? ─────────┬─── Yes ──────> Continue
                             └─── No ───────> "Signature verification failed"
                                              "Tampered or key mismatch"
                                              Exit

License Type Match? ─────────┬─── Yes ──────> Continue
                             └─── No ───────> "Invalid license type"
                                              Exit

Not Expired? ────────────────┬─── Yes ──────> Continue
                             └─── No ───────> "License has expired"
                                              Exit

All Checks Pass ─────────────────────────────> ✓ Start Application
```

## File Format Visualization

```
┌─────────────────────────────────────────────────────────────────┐
│                    License File Structure                        │
└─────────────────────────────────────────────────────────────────┘

Byte Offset    Content                    Hex Example
───────────    ───────                    ───────────

0-3            Magic Bytes "LICF"         4C 49 43 46
4              Version (0x01)             01
5-16           IV (12 bytes)              A3 7F 2E ... (random)
17-32          Auth Tag (16 bytes)        8B 4C 91 ... (computed)
33-end         Encrypted Payload          E7 3A 5F ... (variable)

Total Size: ~500-800 bytes (typical)

Encrypted Payload (after decryption):
────────────────────────────────────
{
  "data": "{\"fingerprint\":\"...\",\"customerName\":\"...\", ...}",
  "signature": "base64_encoded_rsa_signature_here..."
}

Inner Payload (data field):
──────────────────────────
{
  "fingerprint": "abc-123-def-456",
  "customerName": "John Doe",
  "customerEmail": "john@example.com",
  "licenseType": "CameraSoftware",
  "expiry": "2025-12-31T23:59:59.000Z",
  "issuedAt": "2024-01-01T00:00:00.000Z",
  "version": "1.0"
}
```

This comprehensive set of diagrams should help visualize the entire license system flow!
