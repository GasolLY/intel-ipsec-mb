/*******************************************************************************
  Copyright (c) 2012-2017, Intel Corporation

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

      * Redistributions of source code must retain the above copyright notice,
        this list of conditions and the following disclaimer.
      * Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution.
      * Neither the name of Intel Corporation nor the names of its contributors
        may be used to endorse or promote products derived from this software
        without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

/*
 * This contains the bulk of the mb_mgr code, with #define's to build
 * an SSE, AVX, AVX2 or AVX512 version (see mb_mgr_sse.c, mb_mgr_avx.c, etc.)
 *
 * get_next_job() returns a job object. This must be filled in and returned
 * via submit_job() before get_next_job() is called again.
 *
 * submit_job() and flush_job() returns a job object. This job object ceases
 * to be usable at the next call to get_next_job()
 *
 * Assume JOBS() and ADV_JOBS() from mb_mgr_code.h are available
 */

#include <string.h> /* memcpy(), memset() */

/* ========================================================================= */
/* Lower level "out of order" schedulers */
/* ========================================================================= */

__forceinline
JOB_AES_HMAC *
SUBMIT_JOB_AES128_DEC(JOB_AES_HMAC *job)
{
        AES_CBC_DEC_128(job->src + job->cipher_start_src_offset_in_bytes,
                        job->iv,
                        job->aes_dec_key_expanded,
                        job->dst,
                        job->msg_len_to_cipher_in_bytes & (~15));
        job->status |= STS_COMPLETED_AES;
        return job;
}

__forceinline
JOB_AES_HMAC *
SUBMIT_JOB_AES192_DEC(JOB_AES_HMAC *job)
{
        AES_CBC_DEC_192(job->src + job->cipher_start_src_offset_in_bytes,
                        job->iv,
                        job->aes_dec_key_expanded,
                        job->dst,
                        job->msg_len_to_cipher_in_bytes);
        job->status |= STS_COMPLETED_AES;
        return job;
}

__forceinline
JOB_AES_HMAC *
SUBMIT_JOB_AES256_DEC(JOB_AES_HMAC *job)
{
        AES_CBC_DEC_256(job->src + job->cipher_start_src_offset_in_bytes,
                        job->iv,
                        job->aes_dec_key_expanded,
                        job->dst,
                        job->msg_len_to_cipher_in_bytes);
        job->status |= STS_COMPLETED_AES;
        return job;
}

__forceinline
JOB_AES_HMAC *
SUBMIT_JOB_AES128_CNTR(JOB_AES_HMAC *job)
{
        AES_CNTR_128(job->src + job->cipher_start_src_offset_in_bytes,
                     job->iv,
                     job->aes_enc_key_expanded,
                     job->dst,
                     job->msg_len_to_cipher_in_bytes,
                     job->iv_len_in_bytes);
        job->status |= STS_COMPLETED_AES;
        return job;
}

__forceinline
JOB_AES_HMAC *
SUBMIT_JOB_AES192_CNTR(JOB_AES_HMAC *job)
{
        AES_CNTR_192(job->src + job->cipher_start_src_offset_in_bytes,
                     job->iv,
                     job->aes_enc_key_expanded,
                     job->dst,
                     job->msg_len_to_cipher_in_bytes,
                     job->iv_len_in_bytes);
        job->status |= STS_COMPLETED_AES;
        return job;
}

__forceinline
JOB_AES_HMAC *
SUBMIT_JOB_AES256_CNTR(JOB_AES_HMAC *job)
{
        AES_CNTR_256(job->src + job->cipher_start_src_offset_in_bytes,
                     job->iv,
                     job->aes_enc_key_expanded,
                     job->dst,
                     job->msg_len_to_cipher_in_bytes,
                     job->iv_len_in_bytes);
        job->status |= STS_COMPLETED_AES;
        return job;
}

/* ========================================================================= */
/* AES-CCM */
/* ========================================================================= */

__forceinline
void
aes_ccm_ctr_block(void *B0, const void *nonce, const unsigned nonce_len,
                  const uint8_t counter)
{
        const unsigned L = AES_BLOCK_SIZE - 1 - nonce_len;
        uint8_t *a = (uint8_t *)B0;

        /**
         * Current AES-CNTR implementation assumes 4 byte counter.
         * Consequently, AES-CCM will be OK with L from 2 to 4.
         * This limitation results in accepted nonce lengths to be
         * within 13 to 11 range.
         */

        /*
         * Construct IV from nonce, flags and counter of size L.
         * job->iv points to nonce and job->iv_len_in_bytes is nonce length.
         */
        a[0] = (uint8_t) L - 1; /* flags = L` = L - 1 */
        a[AES_BLOCK_SIZE - 1] = counter;
        memcpy(&a[1], nonce, nonce_len);
        memset(&a[1 + nonce_len], 0, L - 1);
}

/* AES-CCM cipher implemented using existing AES-CNTR code */
__forceinline
JOB_AES_HMAC *
SUBMIT_JOB_AES128_CCM_CIPHER(JOB_AES_HMAC *job)
{
        uint8_t a[AES_BLOCK_SIZE];

        aes_ccm_ctr_block(a, job->iv, (unsigned) job->iv_len_in_bytes,
                          1 /* count from 1 */);
        AES_CNTR_128(job->src + job->cipher_start_src_offset_in_bytes, a,
                     job->aes_enc_key_expanded, job->dst,
                     job->msg_len_to_cipher_in_bytes, sizeof(a));
        job->status |= STS_COMPLETED_AES;
        return job;
}

static
JOB_AES_HMAC *
submit_job_aes_ccm_auth(MB_MGR_CBCMAC_OOO *state, JOB_AES_HMAC *job, const unsigned max_jobs)
{
        const unsigned aad_len_size = 2;
        unsigned lane, min_len, min_idx;
        JOB_AES_HMAC *ret_job;
        uint8_t *pb = NULL;
        unsigned i;

        /* get a free lane id */
        lane = state->unused_lanes & 15;
        state->unused_lanes >>= 4;

        if (job->cipher_direction != ENCRYPT) {
                /* now it is time to do the cipher */
                SUBMIT_JOB_AES128_CCM_CIPHER(job);
        }

        /* copy job data in and set up inital blocks */
        pb = &state->init_blocks[lane * 64];
        state->job_in_lane[lane] = job;
        if (job->u.CCM.aad_len_in_bytes != 0) {
                state->lens[lane] = (uint16_t) AES_BLOCK_SIZE +
                        ((job->u.CCM.aad_len_in_bytes + aad_len_size + 15) &
                         (~15));
        } else {
                state->lens[lane] = AES_BLOCK_SIZE;
        }
        state->init_done[lane] = 0;
        state->args.in[lane] = pb;
        state->args.out[lane] = NULL;
        state->args.keys[lane] = job->aes_enc_key_expanded;
        memset(&state->args.IV[lane], 0, sizeof(state->args.IV[0]));

        /* BLOCK 0 */
        aes_ccm_ctr_block(pb, job->iv, (unsigned) job->iv_len_in_bytes,
                          0 /* counter = 0 */);
        /* Correct flags by adding M and AAD presence */
        pb[0] |= (job->u.CCM.aad_len_in_bytes ? 0x40 : 0x00) |
                (((job->auth_tag_output_len_in_bytes - 2) >> 1) << 3);
        /* Message length */
        pb[14] = (uint8_t) (job->msg_len_to_hash_in_bytes >> 8);
        pb[15] = (uint8_t) job->msg_len_to_hash_in_bytes;

        /* AAD blocks, if any */
        if (job->u.CCM.aad_len_in_bytes != 0) {
                memset(&pb[AES_BLOCK_SIZE], 0, 64 - AES_BLOCK_SIZE);
                memcpy(&pb[AES_BLOCK_SIZE + 2], job->u.CCM.aad,
                       job->u.CCM.aad_len_in_bytes);
                pb[AES_BLOCK_SIZE + 0] =
                        (uint8_t) (job->u.CCM.aad_len_in_bytes >> 8);
                pb[AES_BLOCK_SIZE + 1] = (uint8_t) job->u.CCM.aad_len_in_bytes;
        }

        /* enough jobs to start processing? */
        if (state->unused_lanes != 0xf)
                return NULL;
 cbcmac_round:
        /* find min common length to process */
        min_idx = 0;
        min_len = state->lens[0];

        for (i = 1; i < max_jobs; i++) {
                if (min_len > state->lens[i]) {
                        min_idx = i;
                        min_len = state->lens[i];
                }
        }

        /* subtract min len from all lanes */
        for (i = 0; i < max_jobs; i++)
                state->lens[i] -= min_len;

        /* run the algorythmic code on selected blocks */
        if (min_len != 0)
                AES128_CBC_MAC(&state->args, min_len);

        ret_job = state->job_in_lane[min_idx];

        if (state->init_done[min_idx] == 0) {
                if (ret_job->cipher_direction == ENCRYPT) {
                        state->args.in[min_idx] = ret_job->src +
                                ret_job->hash_start_src_offset_in_bytes;
                } else {
                        state->args.in[min_idx] = ret_job->dst;
                }

                state->init_done[min_idx] = 1;

                if (ret_job->msg_len_to_hash_in_bytes & (~15)) {
                        /* first block + AAD done - process message blocks */
                        state->lens[min_idx] =
                                ret_job->msg_len_to_hash_in_bytes & (~15);
                        goto cbcmac_round;
                }
        }

        if (state->init_done[min_idx] == 1 &&
            (ret_job->msg_len_to_hash_in_bytes & 15)) {
                /*
                 * First block, AAD, message blocks are done.
                 * Partial message block is still to do.
                 */
                pb = &state->init_blocks[min_idx * 64];
                state->init_done[min_idx] = 2;
                state->lens[min_idx] = AES_BLOCK_SIZE;
                memset(pb, 0, AES_BLOCK_SIZE);
                memcpy(pb, state->args.in[min_idx],
                       (size_t) ret_job->msg_len_to_hash_in_bytes & 15);
                state->args.in[min_idx] = pb;
                goto cbcmac_round;
        }

        /* Final XOR with AES-CNTR on B_0 */
        pb = &state->init_blocks[min_idx * 64];
        aes_ccm_ctr_block(pb, ret_job->iv, (unsigned) ret_job->iv_len_in_bytes,
                          0 /* counter = 0 */);
        /*
         * Clever use of AES-CTR mode saves a few ops here.
         * What AES-CCM authentication requires us to do is:
         * AES-CCM: E(KEY,B_0) XOR IV_CBC_MAC
         *
         * And what AES_CTR offers is:
         * AES_CTR: E(KEY, NONCE|COUNTER) XOR PLAIN_TEXT
         *
         * So if:
         * B_0 is passed instead of NONCE|COUNTER and IV instead of PLAIN_TESXT
         * then AES_CTR function is doing pretty much what we need.
         * On top of it can truncate the authentication tag and copy to
         * destination.
         */
        AES_CNTR_128(&state->args.IV[min_idx] /* src = IV */,
                     pb /* nonce/iv = B_0 */,
                     state->args.keys[min_idx],
                     ret_job->auth_tag_output /* dst */,
                     ret_job->auth_tag_output_len_in_bytes /* num_bytes */,
                     AES_BLOCK_SIZE /* nonce/iv len */);

        if (ret_job->cipher_direction == ENCRYPT) {
                /* now it is time to do the cipher encrypt */
                SUBMIT_JOB_AES128_CCM_CIPHER(ret_job);
        }

        /* put back processed packet into unused lanes, set job as complete */
        state->unused_lanes = (state->unused_lanes << 4) | min_idx;
        ret_job = state->job_in_lane[min_idx];
        ret_job->status |= STS_COMPLETED_HMAC;
        state->job_in_lane[min_idx] = NULL;
        return ret_job;
}

__forceinline
JOB_AES_HMAC *
submit_job_aes_ccm_auth_arch(MB_MGR_CBCMAC_OOO *state, JOB_AES_HMAC *job)
{
        return submit_job_aes_ccm_auth(state, job, AES_CCM_MAX_JOBS);
}

static
JOB_AES_HMAC *
flush_job_aes_ccm_auth(MB_MGR_CBCMAC_OOO *state, const unsigned max_jobs)
{
        unsigned lane, min_len, min_idx;
        JOB_AES_HMAC *ret_job;
        uint8_t *pb = NULL;
        unsigned i;

        /* find 1st non null job */
        for (lane = 0; lane < max_jobs; lane++)
                if (state->job_in_lane[lane] != NULL)
                        break;
        if (lane >= max_jobs)
                return NULL; /* no not null job */

 cbcmac_flush_round:
        /* copy good lane onto empty lanes */
        for (i = 0; i < max_jobs; i++) {
                if (i == lane)
                        continue;

                if (state->job_in_lane[i] != NULL)
                        continue;

                state->args.in[i] = state->args.in[lane];
                state->args.keys[i] = state->args.keys[lane];
                state->args.IV[i] = state->args.IV[lane];
                state->lens[i] = UINT16_MAX;
                state->init_done[i] = state->init_done[lane];
        }

        /* find min common length to process */
        min_idx = lane;
        min_len = state->lens[lane];

        for (i = 0; i < max_jobs; i++) {
                if (min_len > state->lens[i]) {
                        min_idx = i;
                        min_len = state->lens[i];
                }
        }

        /* subtract min len from all lanes */
        for (i = 0; i < max_jobs; i++)
                state->lens[i] -= min_len;

        /* run the algorythmic code on selected blocks */
        if (min_len != 0)
                AES128_CBC_MAC(&state->args, min_len);

        ret_job = state->job_in_lane[min_idx];

        if (state->init_done[min_idx] == 0) {
                if (ret_job->cipher_direction == ENCRYPT) {
                        state->args.in[min_idx] = ret_job->src +
                                ret_job->hash_start_src_offset_in_bytes;
                } else {
                        state->args.in[min_idx] = ret_job->dst;
                }

                state->init_done[min_idx] = 1;

                if (ret_job->msg_len_to_hash_in_bytes & (~15)) {
                        /* first block + AAD done - process message blocks */
                        state->lens[min_idx] =
                                ret_job->msg_len_to_hash_in_bytes & (~15);
                        goto cbcmac_flush_round;
                }

        }

        if ((state->init_done[min_idx] == 1) &&
            (ret_job->msg_len_to_hash_in_bytes & 15)) {
                /*
                 * First block, AAD, message blocks are done.
                 * Partial message block is still to do.
                 */
                pb = &state->init_blocks[min_idx * 64];
                state->init_done[min_idx] = 2;
                state->lens[min_idx] = AES_BLOCK_SIZE;
                memset(pb, 0, AES_BLOCK_SIZE);
                memcpy(pb, state->args.in[min_idx],
                       (size_t) ret_job->msg_len_to_hash_in_bytes & 15);
                state->args.in[min_idx] = pb;
                goto cbcmac_flush_round;
        }

        /* Final XOR with AES-CNTR on B_0 */
        pb = &state->init_blocks[min_idx * 64];

        aes_ccm_ctr_block(pb, ret_job->iv, (unsigned) ret_job->iv_len_in_bytes,
                          0 /* counter = 0 */);

        /*
         * Clever use of AES-CTR mode saves a few ops here.
         * What AES-CCM authentication requires us to do is:
         * AES-CCM: E(KEY,B_0) XOR IV_CBC_MAC
         *
         * And what AES_CTR offers is:
         * AES_CTR: E(KEY, NONCE|COUNTER) XOR PLAIN_TEXT
         *
         * So if:
         * B_0 is passed instead of NONCE|COUNTER and IV instead of PLAIN_TESXT
         * then AES_CTR function is doing pretty much what we need.
         * On top of it can truncate the authentication tag and copy to
         * destination.
         */
        AES_CNTR_128(&state->args.IV[min_idx] /* src */,
                     pb /* nonce/iv */,
                     state->args.keys[min_idx],
                     ret_job->auth_tag_output /* dst */,
                     ret_job->auth_tag_output_len_in_bytes /* num_bytes */,
                     AES_BLOCK_SIZE /* nonce/iv len */);

        if (ret_job->cipher_direction == ENCRYPT) {
                /* now it is time to do the cipher for real */
                SUBMIT_JOB_AES128_CCM_CIPHER(ret_job);
        }

        /* put back processed packet into unused lanes, set job as complete */
        state->unused_lanes = (state->unused_lanes << 4) | min_idx;
        ret_job->status |= STS_COMPLETED_HMAC;
        state->job_in_lane[min_idx] = NULL;
        state->init_done[min_idx] = 0;
        return ret_job;
}

__forceinline
JOB_AES_HMAC *
flush_job_aes_ccm_auth_arch(MB_MGR_CBCMAC_OOO *state)
{
        return flush_job_aes_ccm_auth(state, AES_CCM_MAX_JOBS);
}

/* ========================================================================= */
/* AES-GCM */
/* ========================================================================= */

#ifndef NO_GCM
__forceinline
JOB_AES_HMAC *
SUBMIT_JOB_AES_GCM_DEC(JOB_AES_HMAC *job)
{
        DECLARE_ALIGNED(struct gcm_context_data ctx, 16);

        if (16 == job->aes_key_len_in_bytes)
                AES_GCM_DEC_128(job->aes_dec_key_expanded, &ctx, job->dst,
                                job->src +
                                job->cipher_start_src_offset_in_bytes,
                                job->msg_len_to_cipher_in_bytes,
                                job->iv,
                                job->u.GCM.aad, job->u.GCM.aad_len_in_bytes,
                                job->auth_tag_output,
                                job->auth_tag_output_len_in_bytes);
        else if (24 == job->aes_key_len_in_bytes)
                AES_GCM_DEC_192(job->aes_dec_key_expanded, &ctx, job->dst,
                                job->src +
                                job->cipher_start_src_offset_in_bytes,
                                job->msg_len_to_cipher_in_bytes,
                                job->iv,
                                job->u.GCM.aad, job->u.GCM.aad_len_in_bytes,
                                job->auth_tag_output,
                                job->auth_tag_output_len_in_bytes);
        else
                AES_GCM_DEC_256(job->aes_dec_key_expanded, &ctx, job->dst,
                                job->src +
                                job->cipher_start_src_offset_in_bytes,
                                job->msg_len_to_cipher_in_bytes,
                                job->iv,
                                job->u.GCM.aad, job->u.GCM.aad_len_in_bytes,
                                job->auth_tag_output,
                                job->auth_tag_output_len_in_bytes);

        job->status = STS_COMPLETED;
        return job;
}

__forceinline
JOB_AES_HMAC *
SUBMIT_JOB_AES_GCM_ENC(JOB_AES_HMAC *job)
{
        DECLARE_ALIGNED(struct gcm_context_data ctx, 16);

        if (16 == job->aes_key_len_in_bytes)
                AES_GCM_ENC_128(job->aes_dec_key_expanded, &ctx, job->dst,
                                job->src +
                                job->cipher_start_src_offset_in_bytes,
                                job->msg_len_to_cipher_in_bytes, job->iv,
                                job->u.GCM.aad, job->u.GCM.aad_len_in_bytes,
                                job->auth_tag_output,
                                job->auth_tag_output_len_in_bytes);
        else if (24 == job->aes_key_len_in_bytes)
                AES_GCM_ENC_192(job->aes_dec_key_expanded, &ctx, job->dst,
                                job->src +
                                job->cipher_start_src_offset_in_bytes,
                                job->msg_len_to_cipher_in_bytes, job->iv,
                                job->u.GCM.aad, job->u.GCM.aad_len_in_bytes,
                                job->auth_tag_output,
                                job->auth_tag_output_len_in_bytes);
        else
                AES_GCM_ENC_256(job->aes_dec_key_expanded, &ctx, job->dst,
                                job->src +
                                job->cipher_start_src_offset_in_bytes,
                                job->msg_len_to_cipher_in_bytes, job->iv,
                                job->u.GCM.aad, job->u.GCM.aad_len_in_bytes,
                                job->auth_tag_output,
                                job->auth_tag_output_len_in_bytes);

        job->status = STS_COMPLETED;
        return job;
}
#endif /* !NO_GCM */

/* ========================================================================= */
/* Custom hash / cipher */
/* ========================================================================= */

__forceinline
JOB_AES_HMAC *
JOB_CUSTOM_CIPHER(JOB_AES_HMAC *job)
{
        if (!(job->status & STS_COMPLETED_AES)) {
                if (job->cipher_func(job))
                        job->status = STS_INTERNAL_ERROR;
                else
                        job->status |= STS_COMPLETED_AES;
        }
        return job;
}

__forceinline
JOB_AES_HMAC *
SUBMIT_JOB_CUSTOM_CIPHER(JOB_AES_HMAC *job)
{
        return JOB_CUSTOM_CIPHER(job);
}

__forceinline
JOB_AES_HMAC *
FLUSH_JOB_CUSTOM_CIPHER(JOB_AES_HMAC *job)
{
        return JOB_CUSTOM_CIPHER(job);
}

__forceinline
JOB_AES_HMAC *
JOB_CUSTOM_HASH(JOB_AES_HMAC *job)
{
        if (!(job->status & STS_COMPLETED_HMAC)) {
                if (job->hash_func(job))
                        job->status = STS_INTERNAL_ERROR;
                else
                        job->status |= STS_COMPLETED_HMAC;
        }
        return job;
}

__forceinline
JOB_AES_HMAC *
SUBMIT_JOB_CUSTOM_HASH(JOB_AES_HMAC *job)
{
        return JOB_CUSTOM_HASH(job);
}

__forceinline
JOB_AES_HMAC *
FLUSH_JOB_CUSTOM_HASH(JOB_AES_HMAC *job)
{
        return JOB_CUSTOM_HASH(job);
}

/* ========================================================================= */
/* DOCSIS AES (AES128 CBC + AES128 CFB) */
/* ========================================================================= */

#define AES_BLOCK_SIZE 16

/**
 * @brief Encrypts/decrypts the last partial block for DOCSIS SEC v3.1 BPI
 *
 * The last partial block is encrypted/decrypted using AES CFB128.
 * IV is always the next last ciphered block.
 *
 * @note It is assumed that length is bigger than one AES 128 block.
 *
 * @param job desriptor of performed crypto operation
 * @return It always returns value passed in \a job
 */
__forceinline
JOB_AES_HMAC *
DOCSIS_LAST_BLOCK(JOB_AES_HMAC *job)
{
        const void *iv = NULL;
        UINT64 offset = 0;
        UINT64 partial_bytes = 0;

        if (job == NULL)
                return job;

        IMB_ASSERT((job->cipher_direction == DECRYPT) ||
                   (job->status & STS_COMPLETED_AES));

        partial_bytes = job->msg_len_to_cipher_in_bytes & (AES_BLOCK_SIZE - 1);
        offset = job->msg_len_to_cipher_in_bytes & (~(AES_BLOCK_SIZE - 1));

        if (!partial_bytes)
                return job;

        /* in either case IV has to be next last ciphered block */
        if (job->cipher_direction == ENCRYPT)
                iv = job->dst + offset - AES_BLOCK_SIZE;
        else
                iv = job->src + job->cipher_start_src_offset_in_bytes +
                        offset - AES_BLOCK_SIZE;

        IMB_ASSERT(partial_bytes <= AES_BLOCK_SIZE);
        AES_CFB_128_ONE(job->dst + offset,
                        job->src + job->cipher_start_src_offset_in_bytes +
                        offset,
                        iv, job->aes_enc_key_expanded, partial_bytes);

        return job;
}

/**
 * @brief Encrypts/decrypts the first and only partial block for
 *        DOCSIS SEC v3.1 BPI
 *
 * The first partial block is encrypted/decrypted using AES CFB128.
 *
 * @param job desriptor of performed crypto operation
 * @return It always returns value passed in \a job
 */
__forceinline
JOB_AES_HMAC *
DOCSIS_FIRST_BLOCK(JOB_AES_HMAC *job)
{
        IMB_ASSERT(!(job->status & STS_COMPLETED_AES));
        IMB_ASSERT(job->msg_len_to_cipher_in_bytes <= AES_BLOCK_SIZE);
        AES_CFB_128_ONE(job->dst,
                        job->src + job->cipher_start_src_offset_in_bytes,
                        job->iv, job->aes_enc_key_expanded,
                        job->msg_len_to_cipher_in_bytes);
        job->status |= STS_COMPLETED_AES;
        return job;
}

/* ========================================================================= */
/* DES and DOCSIS DES (DES CBC + DES CFB) */
/* ========================================================================= */

/**
 * @brief DOCSIS DES cipher encryption
 *
 * @param job desriptor of performed crypto operation
 * @return It always returns value passed in \a job
 */
__forceinline
JOB_AES_HMAC *
DOCSIS_DES_ENC(JOB_AES_HMAC *job)
{
        IMB_ASSERT(!(job->status & STS_COMPLETED_AES));
        docsis_des_enc_basic(job->src + job->cipher_start_src_offset_in_bytes,
                             job->dst,
                             (int) job->msg_len_to_cipher_in_bytes,
                             job->aes_enc_key_expanded,
                             (const uint64_t *)job->iv);
        job->status |= STS_COMPLETED_AES;
        return job;
}

/**
 * @brief DOCSIS DES cipher decryption
 *
 * @param job desriptor of performed crypto operation
 * @return It always returns value passed in \a job
 */
__forceinline
JOB_AES_HMAC *
DOCSIS_DES_DEC(JOB_AES_HMAC *job)
{
        IMB_ASSERT(!(job->status & STS_COMPLETED_AES));
        docsis_des_dec_basic(job->src + job->cipher_start_src_offset_in_bytes,
                             job->dst,
                             (int) job->msg_len_to_cipher_in_bytes,
                             job->aes_dec_key_expanded,
                             (const uint64_t *)job->iv);
        job->status |= STS_COMPLETED_AES;
        return job;
}

/**
 * @brief DES cipher encryption
 *
 * @param job desriptor of performed crypto operation
 * @return It always returns value passed in \a job
 */
__forceinline
JOB_AES_HMAC *
DES_CBC_ENC(JOB_AES_HMAC *job)
{
        IMB_ASSERT(!(job->status & STS_COMPLETED_AES));
        des_enc_cbc_basic(job->src + job->cipher_start_src_offset_in_bytes,
                          job->dst,
                          job->msg_len_to_cipher_in_bytes &
                          (~(DES_BLOCK_SIZE - 1)),
                          job->aes_enc_key_expanded, (const uint64_t *)job->iv);
        job->status |= STS_COMPLETED_AES;
        return job;
}

/**
 * @brief DES cipher decryption
 *
 * @param job desriptor of performed crypto operation
 * @return It always returns value passed in \a job
 */
__forceinline
JOB_AES_HMAC *
DES_CBC_DEC(JOB_AES_HMAC *job)
{
        IMB_ASSERT(!(job->status & STS_COMPLETED_AES));
        des_dec_cbc_basic(job->src + job->cipher_start_src_offset_in_bytes,
                          job->dst,
                          job->msg_len_to_cipher_in_bytes &
                          (~(DES_BLOCK_SIZE - 1)),
                          job->aes_dec_key_expanded, (const uint64_t *)job->iv);
        job->status |= STS_COMPLETED_AES;
        return job;
}

/* ========================================================================= */
/* Cipher submit & flush functions */
/* ========================================================================= */

__forceinline
JOB_AES_HMAC *
SUBMIT_JOB_AES_ENC(MB_MGR *state, JOB_AES_HMAC *job)
{
        if (CBC == job->cipher_mode) {
                if (16 == job->aes_key_len_in_bytes) {
                        return SUBMIT_JOB_AES128_ENC(&state->aes128_ooo, job);
                } else if (24 == job->aes_key_len_in_bytes) {
                        return SUBMIT_JOB_AES192_ENC(&state->aes192_ooo, job);
                } else { /* assume 32 */
                        return SUBMIT_JOB_AES256_ENC(&state->aes256_ooo, job);
                }
        } else if (CNTR == job->cipher_mode) {
                if (16 == job->aes_key_len_in_bytes) {
                        return SUBMIT_JOB_AES128_CNTR(job);
                } else if (24 == job->aes_key_len_in_bytes) {
                        return SUBMIT_JOB_AES192_CNTR(job);
                } else { /* assume 32 */
                        return SUBMIT_JOB_AES256_CNTR(job);
                }
        } else if (DOCSIS_SEC_BPI == job->cipher_mode) {
                if (job->msg_len_to_cipher_in_bytes >= AES_BLOCK_SIZE) {
                        JOB_AES_HMAC *tmp;

                        tmp = SUBMIT_JOB_AES128_ENC(&state->docsis_sec_ooo,
                                                    job);
                        return DOCSIS_LAST_BLOCK(tmp);
                } else
                        return DOCSIS_FIRST_BLOCK(job);
#ifndef NO_GCM
        } else if (GCM == job->cipher_mode) {
                return SUBMIT_JOB_AES_GCM_ENC(job);
#endif /* NO_GCM */
        } else if (CUSTOM_CIPHER == job->cipher_mode) {
                return SUBMIT_JOB_CUSTOM_CIPHER(job);
        } else if (DES == job->cipher_mode) {
#ifdef SUBMIT_JOB_DES_CBC_ENC
                return SUBMIT_JOB_DES_CBC_ENC(&state->des_enc_ooo, job);
#else
                return DES_CBC_ENC(job);
#endif /* SUBMIT_JOB_DES_CBC_ENC */
        } else if (DOCSIS_DES == job->cipher_mode) {
#ifdef SUBMIT_JOB_DOCSIS_DES_ENC
                return SUBMIT_JOB_DOCSIS_DES_ENC(&state->docsis_des_enc_ooo,
                                                 job);
#else
                return DOCSIS_DES_ENC(job);
#endif /* SUBMIT_JOB_DOCSIS_DES_ENC */
        } else { /* assume NUL_CIPHER or CCM */
                job->status |= STS_COMPLETED_AES;
                return job;
        }
}

__forceinline
JOB_AES_HMAC *
FLUSH_JOB_AES_ENC(MB_MGR *state, JOB_AES_HMAC *job)
{
        if (CBC == job->cipher_mode) {
                if (16 == job->aes_key_len_in_bytes) {
                        return FLUSH_JOB_AES128_ENC(&state->aes128_ooo);
                } else if (24 == job->aes_key_len_in_bytes) {
                        return FLUSH_JOB_AES192_ENC(&state->aes192_ooo);
                } else  { /* assume 32 */
                        return FLUSH_JOB_AES256_ENC(&state->aes256_ooo);
                }
        } else if (DOCSIS_SEC_BPI == job->cipher_mode) {
                JOB_AES_HMAC *tmp;

                tmp = FLUSH_JOB_AES128_ENC(&state->docsis_sec_ooo);
                return DOCSIS_LAST_BLOCK(tmp);
#ifdef FLUSH_JOB_DES_CBC_ENC
        } else if (DES == job->cipher_mode) {
                return FLUSH_JOB_DES_CBC_ENC(&state->des_enc_ooo);
#endif /* FLUSH_JOB_DES_CBC_ENC */
#ifdef FLUSH_JOB_DOCSIS_DES_ENC
        } else if (DOCSIS_DES == job->cipher_mode) {
                return FLUSH_JOB_DOCSIS_DES_ENC(&state->docsis_des_enc_ooo);
#endif /* FLUSH_JOB_DOCSIS_DES_ENC */
        } else if (CUSTOM_CIPHER == job->cipher_mode) {
                return FLUSH_JOB_CUSTOM_CIPHER(job);
        } else { /* assume CNTR, CCM or NULL_CIPHER */
                return NULL;
        }
}

__forceinline
JOB_AES_HMAC *
SUBMIT_JOB_AES_DEC(MB_MGR *state, JOB_AES_HMAC *job)
{
        if (CBC == job->cipher_mode) {
                if (16 == job->aes_key_len_in_bytes) {
                        return SUBMIT_JOB_AES128_DEC(job);
                } else if (24 == job->aes_key_len_in_bytes) {
                        return SUBMIT_JOB_AES192_DEC(job);
                } else { /* assume 32 */
                        return SUBMIT_JOB_AES256_DEC(job);
                }
        } else if (CNTR == job->cipher_mode) {
                if (16 == job->aes_key_len_in_bytes) {
                        return SUBMIT_JOB_AES128_CNTR(job);
                } else if (24 == job->aes_key_len_in_bytes) {
                        return SUBMIT_JOB_AES192_CNTR(job);
                } else { /* assume 32 */
                        return SUBMIT_JOB_AES256_CNTR(job);
                }
        } else if (DOCSIS_SEC_BPI == job->cipher_mode) {
                if (job->msg_len_to_cipher_in_bytes >= AES_BLOCK_SIZE) {
                        DOCSIS_LAST_BLOCK(job);
                        return SUBMIT_JOB_AES128_DEC(job);
                } else {
                        return DOCSIS_FIRST_BLOCK(job);
                }
#ifndef NO_GCM
        } else if (GCM == job->cipher_mode) {
                return SUBMIT_JOB_AES_GCM_DEC(job);
#endif /* NO_GCM */
        } else if (DES == job->cipher_mode) {
#ifdef SUBMIT_JOB_DES_CBC_DEC
                return SUBMIT_JOB_DES_CBC_DEC(&state->des_dec_ooo, job);
#else
                (void) state;
                return DES_CBC_DEC(job);
#endif /* SUBMIT_JOB_DES_CBC_DEC */
        } else if (DOCSIS_DES == job->cipher_mode) {
#ifdef SUBMIT_JOB_DOCSIS_DES_DEC
                return SUBMIT_JOB_DOCSIS_DES_DEC(&state->docsis_des_dec_ooo,
                                                 job);
#else
                return DOCSIS_DES_DEC(job);
#endif /* SUBMIT_JOB_DOCSIS_DES_DEC */
        } else if (CUSTOM_CIPHER == job->cipher_mode) {
                return SUBMIT_JOB_CUSTOM_CIPHER(job);
        } else {
                /* assume NULL cipher or CCM */
                job->status |= STS_COMPLETED_AES;
                return job;
        }
}

__forceinline
JOB_AES_HMAC *
FLUSH_JOB_AES_DEC(MB_MGR *state, JOB_AES_HMAC *job)
{
#ifdef FLUSH_JOB_DES_CBC_DEC
        if (DES == job->cipher_mode)
                return FLUSH_JOB_DES_CBC_DEC(&state->des_dec_ooo);
#endif /* FLUSH_JOB_DES_CBC_DEC */
#ifdef FLUSH_JOB_DOCSIS_DES_DEC
        if (DOCSIS_DES == job->cipher_mode)
                return FLUSH_JOB_DOCSIS_DES_DEC(&state->docsis_des_dec_ooo);
#endif /* FLUSH_JOB_DOCSIS_DES_DEC */
        (void) state;
        return SUBMIT_JOB_AES_DEC(state, job);
}

/* ========================================================================= */
/* Hash submit & flush functions */
/* ========================================================================= */

__forceinline
JOB_AES_HMAC *
SUBMIT_JOB_HASH(MB_MGR *state, JOB_AES_HMAC *job)
{
#ifdef VERBOSE
        printf("--------Enter SUBMIT_JOB_HASH --------------\n");
#endif
        switch (job->hash_alg) {
        case SHA1:
#ifdef HASH_USE_SHAEXT
                if (HASH_USE_SHAEXT == SHA_EXT_PRESENT)
                        return SUBMIT_JOB_HMAC_NI(&state->hmac_sha_1_ooo, job);
#endif
                return SUBMIT_JOB_HMAC(&state->hmac_sha_1_ooo, job);
        case SHA_224:
#ifdef HASH_USE_SHAEXT
                if (HASH_USE_SHAEXT == SHA_EXT_PRESENT)
                        return SUBMIT_JOB_HMAC_SHA_224_NI
                                (&state->hmac_sha_224_ooo, job);
#endif
                return SUBMIT_JOB_HMAC_SHA_224(&state->hmac_sha_224_ooo, job);
        case SHA_256:
#ifdef HASH_USE_SHAEXT
                if (HASH_USE_SHAEXT == SHA_EXT_PRESENT)
                        return SUBMIT_JOB_HMAC_SHA_256_NI
                                (&state->hmac_sha_256_ooo, job);
#endif
                return SUBMIT_JOB_HMAC_SHA_256(&state->hmac_sha_256_ooo, job);
        case SHA_384:
                return SUBMIT_JOB_HMAC_SHA_384(&state->hmac_sha_384_ooo, job);
        case SHA_512:
                return SUBMIT_JOB_HMAC_SHA_512(&state->hmac_sha_512_ooo, job);
        case AES_XCBC:
                return SUBMIT_JOB_AES_XCBC(&state->aes_xcbc_ooo, job);
        case MD5:
                return SUBMIT_JOB_HMAC_MD5(&state->hmac_md5_ooo, job);
        case CUSTOM_HASH:
                return SUBMIT_JOB_CUSTOM_HASH(job);
        case AES_CCM:
                return SUBMIT_JOB_AES_CCM_AUTH(&state->aes_ccm_ooo, job);
        default: /* assume NULL_HASH */
                job->status |= STS_COMPLETED_HMAC;
                return job;
        }
}

__forceinline
JOB_AES_HMAC *
FLUSH_JOB_HASH(MB_MGR *state, JOB_AES_HMAC *job)
{
        switch (job->hash_alg) {
        case SHA1:
#ifdef HASH_USE_SHAEXT
                if (HASH_USE_SHAEXT == SHA_EXT_PRESENT)
                        return FLUSH_JOB_HMAC_NI(&state->hmac_sha_1_ooo);
#endif
                return FLUSH_JOB_HMAC(&state->hmac_sha_1_ooo);
        case SHA_224:
#ifdef HASH_USE_SHAEXT
                if (HASH_USE_SHAEXT == SHA_EXT_PRESENT)
                        return FLUSH_JOB_HMAC_SHA_224_NI
                                (&state->hmac_sha_224_ooo);
#endif
                return FLUSH_JOB_HMAC_SHA_224(&state->hmac_sha_224_ooo);
        case SHA_256:
#ifdef HASH_USE_SHAEXT
                if (HASH_USE_SHAEXT == SHA_EXT_PRESENT)
                        return FLUSH_JOB_HMAC_SHA_256_NI
                                (&state->hmac_sha_256_ooo);
#endif
                return FLUSH_JOB_HMAC_SHA_256(&state->hmac_sha_256_ooo);
        case SHA_384:
                return FLUSH_JOB_HMAC_SHA_384(&state->hmac_sha_384_ooo);
        case SHA_512:
                return FLUSH_JOB_HMAC_SHA_512(&state->hmac_sha_512_ooo);
        case AES_XCBC:
                return FLUSH_JOB_AES_XCBC(&state->aes_xcbc_ooo);
        case MD5:
                return FLUSH_JOB_HMAC_MD5(&state->hmac_md5_ooo);
        case CUSTOM_HASH:
                return FLUSH_JOB_CUSTOM_HASH(job);
        case AES_CCM:
                return FLUSH_JOB_AES_CCM_AUTH(&state->aes_ccm_ooo);
        default: /* assume NULL_HASH */
                if (!(job->status & STS_COMPLETED_HMAC)) {
                        job->status |= STS_COMPLETED_HMAC;
                        return job;
                }
                /* if HMAC is complete then return NULL */
                return NULL;
        }
}


/* ========================================================================= */
/* Job submit & flush functions */
/* ========================================================================= */

#ifdef DEBUG
#ifdef _WIN32
#define INVALID_PRN(_fmt, ...)                                          \
        fprintf(stderr, "%s():%d: " _fmt, __FUNCTION__, __LINE__, __VA_ARGS__)
#else
#define INVALID_PRN(_fmt, ...)                                          \
        fprintf(stderr, "%s():%d: " _fmt, __func__, __LINE__, __VA_ARGS__)
#endif
#else
#define INVALID_PRN(_fmt, ...)
#endif

__forceinline int
is_job_invalid(const JOB_AES_HMAC *job)
{
        const UINT64 auth_tag_len_max[] = {
                0,  /* INVALID selection */
                12, /* SHA1 */
                14, /* SHA_224 */
                16, /* SHA_256 */
                24, /* SHA_384 */
                32, /* SHA_512 */
                12, /* AES_XCBC */
                12, /* MD5 */
                0,  /* NULL_HASH */
                16, /* AES_GMAC */
                0,  /* CUSTOM HASH */
                0,  /* AES_CCM */
        };

        switch (job->cipher_mode) {
        case CBC:
                if (job->aes_key_len_in_bytes != UINT64_C(16) &&
                    job->aes_key_len_in_bytes != UINT64_C(24) &&
                    job->aes_key_len_in_bytes != UINT64_C(32)) {
                        INVALID_PRN("cipher_mode:%d\n", job->cipher_mode);
                        return 1;
                }
                if (job->msg_len_to_cipher_in_bytes == 0) {
                        INVALID_PRN("cipher_mode:%d\n", job->cipher_mode);
                        return 1;
                }
                if (job->msg_len_to_cipher_in_bytes & UINT64_C(15)) {
                        INVALID_PRN("cipher_mode:%d\n", job->cipher_mode);
                        return 1;
                }
                if (job->iv_len_in_bytes != UINT64_C(16)) {
                        INVALID_PRN("cipher_mode:%d\n", job->cipher_mode);
                        return 1;
                }
                break;
        case CNTR:
                if (job->aes_key_len_in_bytes != UINT64_C(16) &&
                    job->aes_key_len_in_bytes != UINT64_C(24) &&
                    job->aes_key_len_in_bytes != UINT64_C(32)) {
                        INVALID_PRN("cipher_mode:%d\n", job->cipher_mode);
                        return 1;
                }
                if (job->iv_len_in_bytes != UINT64_C(16) &&
                    job->iv_len_in_bytes != UINT64_C(12)) {
                        INVALID_PRN("cipher_mode:%d\n", job->cipher_mode);
                        return 1;
                }
                if (job->msg_len_to_cipher_in_bytes == 0) {
                        INVALID_PRN("cipher_mode:%d\n", job->cipher_mode);
                        return 1;
                }
                break;
        case NULL_CIPHER:
                /* NULL_CIPHER only allowed in HASH_CIPHER */
                if (job->chain_order != HASH_CIPHER)
                        return 1;
                /* XXX: not copy src to dst */
                break;
        case DOCSIS_SEC_BPI:
                if (job->aes_key_len_in_bytes != UINT64_C(16)) {
                        INVALID_PRN("cipher_mode:%d\n", job->cipher_mode);
                        return 1;
                }
                if (job->iv_len_in_bytes != UINT64_C(16)) {
                        INVALID_PRN("cipher_mode:%d\n", job->cipher_mode);
                        return 1;
                }
                if (job->msg_len_to_cipher_in_bytes == 0) {
                        INVALID_PRN("cipher_mode:%d\n", job->cipher_mode);
                        return 1;
                }
                break;
#ifndef NO_GCM
        case GCM:
                if (job->aes_key_len_in_bytes != UINT64_C(16) &&
                    job->aes_key_len_in_bytes != UINT64_C(24) &&
                    job->aes_key_len_in_bytes != UINT64_C(32)) {
                        INVALID_PRN("cipher_mode:%d\n", job->cipher_mode);
                        return 1;
                }
                if (job->iv_len_in_bytes != UINT64_C(12)) {
                        INVALID_PRN("cipher_mode:%d\n", job->cipher_mode);
                        return 1;
                }
                if (job->hash_alg != AES_GMAC) {
                        INVALID_PRN("cipher_mode:%d\n", job->cipher_mode);
                        return 1;
                }
                if (job->msg_len_to_cipher_in_bytes == 0) {
                        INVALID_PRN("cipher_mode:%d\n", job->cipher_mode);
                        return 1;
                }
                break;
#endif /* !NO_GCM */
        case CUSTOM_CIPHER:
                /* no checks here */
                if (job->cipher_func == NULL) {
                        INVALID_PRN("cipher_mode:%d\n", job->cipher_mode);
                        return 1;
                }
                break;
        case DES:
                if (job->aes_key_len_in_bytes != UINT64_C(8)) {
                        INVALID_PRN("cipher_mode:%d\n", job->cipher_mode);
                        return 1;
                }
                if (job->msg_len_to_cipher_in_bytes == 0) {
                        INVALID_PRN("cipher_mode:%d\n", job->cipher_mode);
                        return 1;
                }
                if (job->msg_len_to_cipher_in_bytes & UINT64_C(7)) {
                        INVALID_PRN("cipher_mode:%d\n", job->cipher_mode);
                        return 1;
                }
                if (job->iv_len_in_bytes != UINT64_C(8)) {
                        INVALID_PRN("cipher_mode:%d\n", job->cipher_mode);
                        return 1;
                }
                break;
        case DOCSIS_DES:
                if (job->aes_key_len_in_bytes != UINT64_C(8)) {
                        INVALID_PRN("cipher_mode:%d\n", job->cipher_mode);
                        return 1;
                }
                if (job->msg_len_to_cipher_in_bytes == 0) {
                        INVALID_PRN("cipher_mode:%d\n", job->cipher_mode);
                        return 1;
                }
                if (job->iv_len_in_bytes != UINT64_C(8)) {
                        INVALID_PRN("cipher_mode:%d\n", job->cipher_mode);
                        return 1;
                }
                break;
        case CCM:
                /* currently only AES-CCM-128 is only supported */
                if (job->aes_key_len_in_bytes != UINT64_C(16)) {
                        INVALID_PRN("cipher_mode:%d\n", job->cipher_mode);
                        return 1;
                }
                /*
                 * From RFC3610:
                 *     Nonce length = 15 - L
                 *     Valid L values are: 2 to 8
                 * Then valid nonce lengths 13 to 7 (inclusive).
                 */
                if (job->iv_len_in_bytes > UINT64_C(13) ||
                    job->iv_len_in_bytes < UINT64_C(7)) {
                        INVALID_PRN("cipher_mode:%d\n", job->cipher_mode);
                        return 1;
                }
                if (job->msg_len_to_cipher_in_bytes == 0) {
                        INVALID_PRN("cipher_mode:%d\n", job->cipher_mode);
                        return 1;
                }
                if (job->hash_alg != AES_CCM) {
                        INVALID_PRN("cipher_mode:%d\n", job->cipher_mode);
                        return 1;
                }
                break;
        default:
                INVALID_PRN("cipher_mode:%d\n", job->cipher_mode);
                return 1;
        }

        switch (job->hash_alg) {
        case SHA1:
        case AES_XCBC:
        case MD5:
        case SHA_224:
        case SHA_256:
        case SHA_384:
        case SHA_512:
                if (job->auth_tag_output_len_in_bytes !=
                    auth_tag_len_max[job->hash_alg]) {
                        INVALID_PRN("hash_alg:%d\n", job->hash_alg);
                        return 1;
                }
                if (job->msg_len_to_hash_in_bytes == 0) {
                        INVALID_PRN("hash_alg:%d\n", job->hash_alg);
                        return 1;
                }
                break;
        case NULL_HASH:
                break;
#ifndef NO_GCM
        case AES_GMAC:
                if (job->auth_tag_output_len_in_bytes != UINT64_C(8) &&
                    job->auth_tag_output_len_in_bytes != UINT64_C(12) &&
                    job->auth_tag_output_len_in_bytes != UINT64_C(16)) {
                        INVALID_PRN("hash_alg:%d\n", job->hash_alg);
                                return 1;
                }
                if (job->cipher_mode != GCM) {
                        INVALID_PRN("hash_alg:%d\n", job->hash_alg);
                                return 1;
                }
                /*
                 * msg_len_to_hash_in_bytes not checked against zero.
                 * It is not used for AES-GCM & GMAC - see
                 * SUBMIT_JOB_AES_GCM_ENC and SUBMIT_JOB_AES_GCM_DEC functions.
                 */
                break;
#endif /* !NO_GCM */
        case CUSTOM_HASH:
                if (job->hash_func == NULL) {
                        INVALID_PRN("hash_alg:%d\n", job->hash_alg);
                        return 1;
                }
                break;
        case AES_CCM:
                if (job->u.CCM.aad_len_in_bytes > 46) {
                        /* 3 x AES_BLOCK - 2 bytes for AAD len */
                        INVALID_PRN("hash_alg:%d\n", job->hash_alg);
                        return 1;
                }
                if ((job->u.CCM.aad_len_in_bytes > 0) &&
                    (job->u.CCM.aad == NULL)) {
                        INVALID_PRN("hash_alg:%d\n", job->hash_alg);
                        return 1;
                }
                /* M can be any even number from 4 to 16 */
                if (job->auth_tag_output_len_in_bytes < UINT64_C(4) ||
                    job->auth_tag_output_len_in_bytes > UINT64_C(16) ||
                    ((job->auth_tag_output_len_in_bytes & 1) != 0)) {
                        INVALID_PRN("hash_alg:%d\n", job->hash_alg);
                                return 1;
                }
                if (job->cipher_mode != CCM) {
                        INVALID_PRN("hash_alg:%d\n", job->hash_alg);
                        return 1;
                }
                /*
                 * AES-CCM allows for only one message for
                 * cipher and uthentication.
                 * AAD can be used to extend authentication over
                 * clear text fields.
                 */
                if (job->msg_len_to_cipher_in_bytes !=
                    job->msg_len_to_hash_in_bytes) {
                        INVALID_PRN("hash_alg:%d\n", job->hash_alg);
                        return 1;
                }
                if (job->cipher_start_src_offset_in_bytes !=
                    job->hash_start_src_offset_in_bytes) {
                        INVALID_PRN("hash_alg:%d\n", job->hash_alg);
                        return 1;
                }
                break;
        default:
                INVALID_PRN("hash_alg:%d\n", job->hash_alg);
                return 1;
        }

        switch (job->chain_order) {
        case CIPHER_HASH:
                if (job->cipher_direction != ENCRYPT) {
                        INVALID_PRN("chain_order:%d\n", job->chain_order);
                        return 1;
                }
                break;
        case HASH_CIPHER:
                if (job->cipher_mode != NULL_CIPHER) {
                        if (job->cipher_direction != DECRYPT) {
                                INVALID_PRN("chain_order:%d\n",
                                            job->chain_order);
                                return 1;
                        }
                }
                break;
        default:
                INVALID_PRN("chain_order:%d\n", job->chain_order);
                return 1;
        }

        return 0;
}

__forceinline
JOB_AES_HMAC *submit_new_job(MB_MGR *state, JOB_AES_HMAC *job)
{
        if (job->chain_order == CIPHER_HASH) {
                /* assume job->cipher_direction == ENCRYPT */
                job = SUBMIT_JOB_AES_ENC(state, job);
                if (job) {
                        job = SUBMIT_JOB_HASH(state, job);
                        if (job && (job->chain_order == HASH_CIPHER))
                                SUBMIT_JOB_AES_DEC(state, job);
                } /* end if job */
        } else { /* job->chain_order == HASH_CIPHER */
                /* assume job->cipher_direction == DECRYPT */
                job = SUBMIT_JOB_HASH(state, job);
                if (job && (job->chain_order == HASH_CIPHER))
                        SUBMIT_JOB_AES_DEC(state, job);
        }
        return job;
}

__forceinline
void complete_job(MB_MGR *state, JOB_AES_HMAC *job)
{
        JOB_AES_HMAC *tmp = NULL;

        while (job->status < STS_COMPLETED) {
                if (job->chain_order == CIPHER_HASH) {
                        /* assume job->cipher_direction == ENCRYPT */
                        tmp = FLUSH_JOB_AES_ENC(state, job);
                        if (tmp)
                                tmp = SUBMIT_JOB_HASH(state, tmp);
                        else
                                tmp = FLUSH_JOB_HASH(state, job);
                        if (tmp && (tmp->chain_order == HASH_CIPHER))
                                SUBMIT_JOB_AES_DEC(state, tmp);
                } else { /* job->chain_order == HASH_CIPHER */
                        /* assume job->cipher_direction == DECRYPT */
                        tmp = FLUSH_JOB_HASH(state, job);
                        if (tmp == NULL)
                                tmp = FLUSH_JOB_AES_DEC(state, job);
                        else
                                if (tmp->chain_order == HASH_CIPHER)
                                        SUBMIT_JOB_AES_DEC(state, tmp);
                }
        }
}

__forceinline
JOB_AES_HMAC *
submit_job_and_check(MB_MGR *state, const int run_check)
{
        JOB_AES_HMAC *job = NULL;
#ifndef LINUX
        DECLARE_ALIGNED(UINT128 xmm_save[10], 16);

        SAVE_XMMS(xmm_save);
#endif

        job = JOBS(state, state->next_job);

        if (run_check) {
                if (is_job_invalid(job)) {
                        job->status = STS_INVALID_ARGS;
                } else {
                        job->status = STS_BEING_PROCESSED;
                        job = submit_new_job(state, job);
                }
        } else {
                job->status = STS_BEING_PROCESSED;
                job = submit_new_job(state, job);
        }

        if (state->earliest_job < 0) {
                /* state was previously empty */
                state->earliest_job = state->next_job;
                ADV_JOBS(&state->next_job);
#ifndef LINUX
                RESTORE_XMMS(xmm_save);
#endif
                return NULL;	/* if we were empty, nothing to return */
        }

        ADV_JOBS(&state->next_job);

        if (state->earliest_job == state->next_job) {
                /* Full */
                job = JOBS(state, state->earliest_job);
                complete_job(state, job);
                ADV_JOBS(&state->earliest_job);
#ifndef LINUX
                RESTORE_XMMS(xmm_save);
#endif
                return job;
        }

        /* not full */
#ifndef LINUX
        RESTORE_XMMS(xmm_save);
#endif
        job = JOBS(state, state->earliest_job);
        if (job->status < STS_COMPLETED)
                return NULL;

        ADV_JOBS(&state->earliest_job);
        return job;
}

IMB_DLL_EXPORT
JOB_AES_HMAC *
SUBMIT_JOB(MB_MGR *state)
{
        return submit_job_and_check(state, 1);
}

IMB_DLL_EXPORT
JOB_AES_HMAC *
SUBMIT_JOB_NOCHECK(MB_MGR *state)
{
        return submit_job_and_check(state, 0);
}

IMB_DLL_EXPORT
JOB_AES_HMAC *
FLUSH_JOB(MB_MGR *state)
{
        JOB_AES_HMAC *job;
#ifndef LINUX
        DECLARE_ALIGNED(UINT128 xmm_save[10], 16);
#endif

        if (state->earliest_job < 0)
                return NULL; /* empty */

#ifndef LINUX
        SAVE_XMMS(xmm_save);
#endif
        job = JOBS(state, state->earliest_job);
        complete_job(state, job);

        ADV_JOBS(&state->earliest_job);

        if (state->earliest_job == state->next_job)
                state->earliest_job = -1; /* becomes empty */

#ifndef LINUX
        RESTORE_XMMS(xmm_save);
#endif
        return job;
}

/* ========================================================================= */
/* ========================================================================= */

IMB_DLL_EXPORT
UINT32
QUEUE_SIZE(MB_MGR *state)
{
        int a, b;

        if (state->earliest_job < 0)
                return 0;
        a = state->next_job / sizeof(JOB_AES_HMAC);
        b = state->earliest_job / sizeof(JOB_AES_HMAC);
        return ((a-b) & (MAX_JOBS-1));
}
