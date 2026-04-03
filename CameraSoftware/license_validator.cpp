#include "license_validator.h"
#include "cJSON.h"
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QDebug>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/pem.h>
#include <openssl/err.h>

static const char* PUBLIC_KEY_PEM = 
"-----BEGIN PUBLIC KEY-----\n"
"MIICIjANBgkqhkiG9w0BAQEFAAOCAg8AMIICCgKCAgEAqE8++s+Wt6wzQUZsEwec\n"
"rE91VZbLnrl3HGZ7Ni5RmzpDjyJqOXeH+O1PF6rg4DXvr2ZCP1c1yK8w9/R7f/Ei\n"
"crjMdg0zia7X0qyfI156b3gPe0hY0zBCmppM37hNxHVrn3OlK2DegTYWSAvgRkGg\n"
"LCS62bRQxUHdlwy8wXfaUYY4NJMywNIWhGrs/CSpDXem9iDGr00Mte9PdvsYY7UT\n"
"ybSMgJaI+mt0iC6yZWm+9+98gxcG+BCreoI3N0rCSC6GTnM8GxHHnPC+ehEHSipI\n"
"EXzj1NXYMq2P+/VWfLs2B6w9tSKgKZyBjSmshBHabxGkKpIN5XYsNxOiPgMzirHY\n"
"HxhIjWCWh+DHaUM8ADD09QVJPZY6s5wsgkcTSk8RTHGbn7wEyggZm2SJC4gca9yY\n"
"D8Z9TnG5PoKvPpcv9cGMQGQCKk549uPsMkXUS/LnvJem4mNeO2zM010FZh2y9VPH\n"
"MZjDT1V0Lovwf5Y6+SNEaRpCXq/4urvlj09OKbtKEx1lJmIu6bB9VKBp7b7r/kKS\n"
"c4daHFEpubBIl1qht/5wQ7CKv5u0lfOlKBPWd2X52cznBLkvZ1GIe7pmiP2tVMYO\n"
"PeTrTjla+rCyonHfuRtuWkWcnxUSjpSxb0nfNdgztOQGym6IE/MQhUwfcsSRU2by\n"
"ntIk+4MhueYiJR1NhlgQgjsCAwEAAQ==\n"
"-----END PUBLIC KEY-----\n";

QString LicenseValidator::getMachineFingerprint() {
    QFile file("/sys/class/dmi/id/product_uuid");
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString uuid = QString::fromUtf8(file.readAll()).trimmed();
        if (!uuid.isEmpty()) return uuid;
    }
    file.setFileName("/etc/machine-id");
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString id = QString::fromUtf8(file.readAll()).trimmed();
        if (!id.isEmpty()) return id;
    }
    return QString("UNKNOWN_FINGERPRINT");
}

QByteArray LicenseValidator::deriveHardwareKey(const QString& fingerprint) {
    QByteArray fpData = fingerprint.toUtf8();
    QByteArray hash(SHA256_DIGEST_LENGTH, 0);
    SHA256((unsigned char*)fpData.data(), fpData.size(), (unsigned char*)hash.data());
    return hash;
}

bool LicenseValidator::verifyRsaSignature(const QByteArray& payloadString, const QByteArray& signatureBase64) {
    BIO* bio = BIO_new_mem_buf(PUBLIC_KEY_PEM, -1);
    EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (!pkey) return false;

    // Decode Base64 signature
    QByteArray sigDecoded = QByteArray::fromBase64(signatureBase64);

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    EVP_VerifyInit_ex(mdctx, EVP_sha256(), NULL);
    EVP_VerifyUpdate(mdctx, payloadString.constData(), payloadString.size());
    int result = EVP_VerifyFinal(mdctx, (unsigned char*)sigDecoded.constData(), sigDecoded.size(), pkey);
    
    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);
    
    return (result == 1);
}

bool LicenseValidator::validate(const QString& licensePath) {
    if (!QFileInfo::exists(licensePath)) {
        qCritical() << "License file not found at" << licensePath;
        return false;
    }

    QFile file(licensePath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QByteArray fileData = file.readAll();
    file.close();

    // 1. Verify Magic Bytes & Version (4 bytes "LICF", 1 byte version)
    if (fileData.size() < 33 || !fileData.startsWith("LICF\x01")) {
        qCritical() << "Invalid license file format.";
        return false;
    }

    // 2. Extract AES components
    QByteArray iv = fileData.mid(5, 12);
    QByteArray authTag = fileData.mid(17, 16);
    QByteArray ciphertext = fileData.mid(33);

    // 3. Derive AES Key from Hardware Fingerprint
    QByteArray key = deriveHardwareKey(getMachineFingerprint());

    // 4. AES-256-GCM Decryption
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL);
    EVP_DecryptInit_ex(ctx, NULL, NULL, (unsigned char*)key.data(), (unsigned char*)iv.data());
    
    QByteArray plaintext(ciphertext.size(), 0);
    int outLen = 0, finalLen = 0;
    
    EVP_DecryptUpdate(ctx, (unsigned char*)plaintext.data(), &outLen, (unsigned char*)ciphertext.data(), ciphertext.size());
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void*)authTag.data());
    
    int ret = EVP_DecryptFinal_ex(ctx, (unsigned char*)plaintext.data() + outLen, &finalLen);
    EVP_CIPHER_CTX_free(ctx);

    if (ret <= 0) {
        qCritical() << "Hardware binding failed! This license is for a different machine or tampered.";
        return false;
    }
    plaintext.resize(outLen + finalLen);

    // 5. Parse Decrypted JSON Container
    cJSON* root = cJSON_Parse(plaintext.constData());
    if (!root) return false;

    cJSON* dataObj = cJSON_GetObjectItem(root, "data");
    cJSON* sigObj = cJSON_GetObjectItem(root, "signature");

    if (!dataObj || !sigObj || !cJSON_IsString(dataObj) || !cJSON_IsString(sigObj)) {
        cJSON_Delete(root);
        return false;
    }

    QByteArray payloadStr(dataObj->valuestring);
    QByteArray signatureBase64(sigObj->valuestring);

    // 6. Verify RSA Signature
    if (!verifyRsaSignature(payloadStr, signatureBase64)) {
        qCritical() << "License signature verification failed!";
        cJSON_Delete(root);
        return false;
    }

    // 7. Parse inner Payload and validate dates & type
    cJSON* payloadJson = cJSON_Parse(payloadStr.constData());
    if (!payloadJson) {
        cJSON_Delete(root);
        return false;
    }

    cJSON* typeNode = cJSON_GetObjectItem(payloadJson, "licenseType");
    if (!typeNode || QString(typeNode->valuestring) != "CameraSoftware") {
        qCritical() << "Invalid license type for this software.";
        cJSON_Delete(payloadJson);
        cJSON_Delete(root);
        return false;
    }

    cJSON* expiryNode = cJSON_GetObjectItem(payloadJson, "expiry");
    if (expiryNode && cJSON_IsString(expiryNode)) {
        // If expiry is null string or valid
        QString expiryStr(expiryNode->valuestring);
        if (!expiryStr.isEmpty() && expiryStr != "null") {
            QDateTime expiryDate = QDateTime::fromString(expiryStr, Qt::ISODate);
            if (expiryDate.isValid() && QDateTime::currentDateTimeUtc() > expiryDate) {
                qCritical() << "License has expired!";
                cJSON_Delete(payloadJson);
                cJSON_Delete(root);
                return false;
            }
        }
    }

    cJSON_Delete(payloadJson);
    cJSON_Delete(root);
    return true; // Valid
}