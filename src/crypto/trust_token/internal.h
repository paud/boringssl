/* Copyright (c) 2019, Google Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */

#ifndef OPENSSL_HEADER_TRUST_TOKEN_INTERNAL_H
#define OPENSSL_HEADER_TRUST_TOKEN_INTERNAL_H

#include <openssl/base.h>
#include <openssl/ec.h>
#include <openssl/ec_key.h>
#include <openssl/nid.h>

#include "../fipsmodule/ec/internal.h"

#include <openssl/trust_token.h>


#if defined(__cplusplus)
extern "C" {
#endif


// PMBTokens.
//
// PMBTokens is described in https://eprint.iacr.org/2020/072/20200324:214215
// and provides anonymous tokens with private metadata. We implement the
// construction with validity verification, described in appendix H,
// construction 6, using P-521 as the group.

// PMBTOKEN_NONCE_SIZE is the size of nonces used as part of the PMBToken
// protocol.
#define PMBTOKEN_NONCE_SIZE 64

typedef struct {
  EC_RAW_POINT pub0;
  EC_RAW_POINT pub1;
  EC_RAW_POINT pubs;
} PMBTOKEN_CLIENT_KEY;

typedef struct {
  EC_SCALAR x0;
  EC_SCALAR y0;
  EC_SCALAR x1;
  EC_SCALAR y1;
  EC_SCALAR xs;
  EC_SCALAR ys;
  EC_RAW_POINT pub0;
  EC_RAW_POINT pub1;
  EC_RAW_POINT pubs;
} PMBTOKEN_ISSUER_KEY;

// PMBTOKEN_PRETOKEN represents the intermediate state a client keeps during a
// PMBToken issuance operation.
typedef struct pmb_pretoken_st {
  uint8_t t[PMBTOKEN_NONCE_SIZE];
  EC_SCALAR r;
  EC_RAW_POINT Tp;
} PMBTOKEN_PRETOKEN;

// PMBTOKEN_PRETOKEN_free releases the memory associated with |token|.
OPENSSL_EXPORT void PMBTOKEN_PRETOKEN_free(PMBTOKEN_PRETOKEN *token);

DEFINE_STACK_OF(PMBTOKEN_PRETOKEN)

// pmbtoken_generate_key generates a fresh keypair and writes their serialized
// forms into |out_private| and |out_public|. It returns one on success and zero
// on failure.
int pmbtoken_generate_key(CBB *out_private, CBB *out_public);

// pmbtoken_client_key_from_bytes decodes a client key from |in| and sets |key|
// to the resulting key. It returns one on success and zero
// on failure.
int pmbtoken_client_key_from_bytes(PMBTOKEN_CLIENT_KEY *key, const uint8_t *in,
                                   size_t len);

// pmbtoken_issuer_key_from_bytes decodes a issuer key from |in| and sets |key|
// to the resulting key. It returns one on success and zero
// on failure.
int pmbtoken_issuer_key_from_bytes(PMBTOKEN_ISSUER_KEY *key, const uint8_t *in,
                                   size_t len);

// pmbtoken_blind generates a new issuance request for |count| tokens. On
// success, it returns a newly-allocated |STACK_OF(PMBTOKEN_PRETOKEN)| and
// writes a request to the issuer to |cbb|. On failure, it returns NULL. The
// |STACK_OF(PMBTOKEN_PRETOKEN)|s should be passed to |pmbtoken_unblind| when
// the server responds.
//
// This function implements the AT.Usr0 operation.
STACK_OF(PMBTOKEN_PRETOKEN) *pmbtoken_blind(CBB *cbb, size_t count);

// pmbtoken_sign parses a request for |num_requested| tokens from |cbs| and
// issues |num_to_issue| tokens with |key| and a private metadata value of
// |private_metadata|. It then writes the response to |cbb|. It returns one on
// success and zero on failure.
//
// This function implements the AT.Sig operation.
int pmbtoken_sign(const PMBTOKEN_ISSUER_KEY *key, CBB *cbb, CBS *cbs,
                  size_t num_requested, size_t num_to_issue,
                  uint8_t private_metadata);

// pmbtoken_unblind processes an issuance response for |count| tokens from |cbs|
// and unblinds the signed tokens. |pretokens| are the pre-tokens returned from
// the corresponding |pmbtoken_blind| call. On success, the function returns a
// newly-allocated |STACK_OF(TRUST_TOKEN)| containing the resulting tokens. Each
// token's serialization will have |key_id| prepended. Otherwise, it returns
// NULL.
//
// This function implements the AT.Usr1 operation.
STACK_OF(TRUST_TOKEN) *
    pmbtoken_unblind(const PMBTOKEN_CLIENT_KEY *key,
                     const STACK_OF(PMBTOKEN_PRETOKEN) *pretokens, CBS *cbs,
                     size_t count, uint32_t key_id);

// pmbtoken_read parses a PMBToken from |token| and verifies it using |key|. On
// success, it returns one and stores the nonce and private metadata bit in
// |out_nonce| and |*out_private_metadata|. Otherwise, it returns zero. Note
// that, unlike the output of |pmbtoken_unblind|, |token| does not have a
// four-byte key ID prepended.
int pmbtoken_read(const PMBTOKEN_ISSUER_KEY *key,
                  uint8_t out_nonce[PMBTOKEN_NONCE_SIZE],
                  uint8_t *out_private_metadata, const uint8_t *token,
                  size_t token_len);



// Trust Tokens internals.

struct trust_token_method_st {
  // TODO(davidben): Add functions here to swap out the PMBTokens mechanism.
  char empty;
};

// Structure representing a single Trust Token public key with the specified ID.
struct trust_token_client_key_st {
  uint32_t id;
  PMBTOKEN_CLIENT_KEY key;
};

// Structure representing a single Trust Token private key with the specified
// ID.
struct trust_token_issuer_key_st {
  uint32_t id;
  PMBTOKEN_ISSUER_KEY key;
};

struct trust_token_client_st {
  const TRUST_TOKEN_METHOD *method;

  // max_batchsize is the maximum supported batchsize.
  uint16_t max_batchsize;

  // keys is the set of public keys that are supported by the client for
  // issuance/redemptions.
  struct trust_token_client_key_st keys[3];

  // num_keys is the number of keys currently configured.
  size_t num_keys;

  // pretokens is the intermediate state during an active issuance.
  STACK_OF(PMBTOKEN_PRETOKEN)* pretokens;

  // srr_key is the public key used to verify the signature of the SRR.
  EVP_PKEY *srr_key;
};


struct trust_token_issuer_st {
  const TRUST_TOKEN_METHOD *method;

  // max_batchsize is the maximum supported batchsize.
  uint16_t max_batchsize;

  // keys is the set of private keys that are supported by the issuer for
  // issuance/redemptions. The public metadata is an index into this list of
  // keys.
  struct trust_token_issuer_key_st keys[3];

  // num_keys is the number of keys currently configured.
  size_t num_keys;

  // srr_key is the private key used to sign the SRR.
  EVP_PKEY *srr_key;

  // metadata_key is the secret material used to encode the private metadata bit
  // in the SRR.
  uint8_t *metadata_key;
  size_t metadata_key_len;
};


#if defined(__cplusplus)
}  // extern C

extern "C++" {

BSSL_NAMESPACE_BEGIN

BORINGSSL_MAKE_DELETER(PMBTOKEN_PRETOKEN, PMBTOKEN_PRETOKEN_free)

BSSL_NAMESPACE_END

}  // extern C++
#endif

#endif  // OPENSSL_HEADER_TRUST_TOKEN_INTERNAL_H
