/*
 * Copyright (c) 2012 The Regents of The University of California
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * Author: Duy Nguyen(duy@soe.ucsc.edu)
 */


#ifndef __RC_TERA_H
#define __RC_TERA_H

#define TERA_SCALE  16
#define TERA_FRAC(val, div) (((val) << TERA_SCALE) / div)
#define TERA_TRUNC(val) ((val) >> TERA_SCALE)


struct tera_rate {
	int bitrate;
	int rix;

	unsigned int perfect_tx_time;
	unsigned int ack_time;

	int sample_limit;
	unsigned int retry_count;
	unsigned int retry_count_cts;
	unsigned int retry_count_rtscts;
	unsigned int adjusted_retry_count;

	u32 success;
	u32 attempts;
	u32 last_attempts;
	u32 last_success;

	/* parts per thousand */
	u32 cur_prob;
	u32 probability;

	/* per-rate throughput */
	u32 cur_tp;

	u64 succ_hist;
	u64 att_hist;
};

struct tera_sta_info {
	unsigned long stats_update;
	unsigned long stats_update_adaptive;
	unsigned int sp_ack_dur;
	unsigned int rate_avg;

	unsigned int lowest_rix;

	u32 tera_tx_ok;
	u32 tera_tx_err;
	u32 tera_tx_retr;
	u32 tera_tx_credit;
	u32 last_tp;

	unsigned int cur_rateidx;
	unsigned int last_rateidx;
	unsigned int max_tp_rate;
	unsigned int max_tp_rate2;

	unsigned int tera_avg_tp;
	unsigned int tera_ossilate;
	unsigned int tera_successive;
	unsigned int tera_consecutive;
	unsigned int tera_time_interval;

	unsigned int max_prob_rate;
	unsigned int packet_count;
	unsigned int sample_count;
	int sample_deferred;

	unsigned int sample_idx;
	unsigned int sample_column;

	int n_rates;
	struct tera_rate *r;
	bool prev_sample;
	bool isProbing;
	bool isMultiplicative;


#ifdef CONFIG_MAC80211_DEBUGFS
	struct dentry *dbg_stats;
#endif
};

struct tera_priv {
	struct ieee80211_hw *hw;
	bool has_mrr;
	unsigned int cw_min;
	unsigned int cw_max;
	unsigned int max_retry;
	unsigned int ewma_level;
	unsigned int segment_size;
	unsigned int update_interval;
	unsigned int lookaround_rate;
	unsigned int lookaround_rate_mrr;
	unsigned int ath_rate_raise;
	
};

void tera_add_sta_debugfs(void *priv, void *priv_sta, struct dentry *dir);
void tera_remove_sta_debugfs(void *priv, void *priv_sta);

#endif
