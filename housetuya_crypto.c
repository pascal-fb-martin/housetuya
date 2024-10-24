/* HouseTuya - A simple web service for control of Tuya devices
 *
 * Copyright 2024, Pascal Martin
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 *
 * housetuya_crypto.c - Cryptographic support for the Tuya protocol
 *
 * SYNOPSYS:
 *
 * const char *housetuya_discoverykey (void);
 *
 *    Return the hardcoded Tuya discovery message key.
 *
 * int housetuya_encrypt (const unsigned char *key,
 *                        char *encrypted, char *clear, int length);
 * int housetuya_decrypt (const unsigned char *key,
 *                        const char *encrypted, char *clear, int length);
 *
 *    Encrypt and decrypt a Tuya message.
 */

#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <openssl/evp.h>

#include "housetuya.h"
#include "housetuya_crypto.h"

#define DEBUG if (housetuya_isdebug()) printf

static char TuyaDiscoveryPassword[] = "yGAdlopoPVldABfn";
static unsigned char TuyaDiscoveryKey[EVP_MAX_MD_SIZE] = {0};

static void TuyaMd5Digest (const char *password, unsigned char *digest) {
  int length;
  EVP_MD_CTX *md5 = EVP_MD_CTX_new();

  EVP_DigestInit(md5, EVP_md5());
  EVP_DigestUpdate(md5, password, strlen(password));
  EVP_DigestFinal_ex(md5, digest, &length);
  if (length != 16) {
      DEBUG ("** unexpected MD5 digest length %d\n", length);
  }
  EVP_MD_CTX_free (md5);
}

const char *housetuya_discoverykey (void) {

    static int Initialized = 0;
    if (! Initialized) {
        TuyaMd5Digest (TuyaDiscoveryPassword, TuyaDiscoveryKey);
        Initialized = 1;
    }
    return TuyaDiscoveryKey;
}

int housetuya_encrypt (const unsigned char *key,
                       char *encrypted, char *clear, int length) {
    int cursor, crypted_length;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return 0;
    if (!EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), 0, key, 0)) {
        DEBUG ("** EVP_DecryptInit_ex error\n");
        return 0;
    }
    EVP_CIPHER_CTX_set_key_length (ctx, 16);
    if (!EVP_EncryptUpdate(ctx, encrypted, &cursor, clear, length)) {
        DEBUG ("** EVP_EncryptUpdate error\n");
        return 0;
    }
    crypted_length = cursor;
    if (!EVP_EncryptFinal_ex(ctx, encrypted+cursor, &cursor)) {
        DEBUG ("** EVP_EncryptFinal_ex error\n");
        return 0;
    }
    crypted_length += cursor;
    DEBUG ("Length after encoding: %d\n", crypted_length);
    EVP_CIPHER_CTX_free(ctx);
    return crypted_length;
}

int housetuya_decrypt (const unsigned char *key,
                       const char *encrypted, char *clear, int length) {
    int cursor, clear_length;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return 0;
    if (!EVP_DecryptInit_ex(ctx, EVP_aes_128_ecb(), 0, key, 0)) {
        DEBUG ("** EVP_DecryptInit_ex error\n");
        return 0;
    }
    EVP_CIPHER_CTX_set_key_length (ctx, 16);
    if (!EVP_DecryptUpdate(ctx, clear, &cursor, encrypted, length)) {
        DEBUG ("** EVP_DecryptUpdate error\n");
        return 0;
    }
    clear_length = cursor;
    if (!EVP_DecryptFinal_ex(ctx, clear+cursor, &cursor)) {
        DEBUG ("** EVP_DecryptFinal_ex error\n");
        return 0;
    }
    clear_length += cursor;
    // Remove AES padding, if any.
    if ((clear[clear_length-1] > 0) && (clear[clear_length-1] < 16))
       clear_length -= clear[clear_length-1];
    clear[clear_length] = 0;
    DEBUG ("Length after decoding: %d\n", clear_length);
    EVP_CIPHER_CTX_free(ctx);
    return clear_length;
}

