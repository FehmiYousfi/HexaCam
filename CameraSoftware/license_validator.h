#ifndef LICENSE_VALIDATOR_H
#define LICENSE_VALIDATOR_H

#include <QString>
#include <QByteArray>

class LicenseValidator {
public:
    // Attempts to load, decrypt, and verify the license file
    // Returns true if valid, false if invalid, expired, or tampered.
    static bool validate(const QString& licensePath);

private:
    static QString getMachineFingerprint();
    static QByteArray deriveHardwareKey(const QString& fingerprint);
    static bool verifyRsaSignature(const QByteArray& payloadString, const QByteArray& signatureBase64);
};

#endif // LICENSE_VALIDATOR_H