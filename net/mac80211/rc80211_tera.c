/*
 * Copyright (c) 2012 The Regents of the University of California
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * Author: Duy Nguyen(duy@soe.ucsc.edu)
 */

/*
 *
 * Copyright (C) 2008 Felix Fietkau <nbd@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Copyright (C) 2005-2007 Derek Smithies <derek@indranet.co.nz>
 *   Sponsored by Indranet Technologies Ltd
 *
 * Copyright (c) 2002-2005 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 */
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/debugfs.h>
#include <linux/random.h>
#include <linux/ieee80211.h>
#include <net/mac80211.h>
#include "rate.h"
#include "rc80211_tera.h"


/*
 * Perform EWMA (Exponentially Weighted Moving Average) calculation
 */
static int
tera_ewma(int old, int new, int weight)
{
        return (new * (100 - weight) + old * weight) / 100;
}


/* convert mac80211 rate index to local array index */
static inline int
rix_to_ndx(struct tera_sta_info *mi, int rix)
{
	int i = rix;
	for (i = rix; i >= 0; i--)
		if (mi->r[i].rix == rix)
			break;
	WARN_ON(i < 0);
	return i;
}
static void
tera_check_rate(struct tera_sta_info *mi)
{

	int rate;
	struct tera_rate *mr = &mi->r[mi->cur_rateidx];

	rate = mi->cur_rateidx;

        if (mi->tera_consecutive > 1)
                mi->isMultiplicative = true;
        else
                mi->isMultiplicative = false;

	if (mi->isProbing) {
		mi->isProbing = false;
		if (mr->cur_tp < mi->tera_avg_tp && mi->last_rateidx != rate && mr->cur_tp != 0) {
			mi->tera_consecutive = 0;
			rate = mi->last_rateidx;
			if (rate != mi->cur_rateidx)
				mi->cur_rateidx = rate;
			mi->tera_ossilate++;

			if (mi->isMultiplicative == false) {
				mi->tera_time_interval = 900;
				//printk (KERN_DEBUG "reset 900ms\n");
			}
			else if (mi->isMultiplicative == true){
				mi->tera_time_interval = 100;
				//printk (KERN_DEBUG "multipli reset 100ms\n");
			}
		}	
		else if (mr->cur_tp > mi->last_tp && mi->last_rateidx != rate ) {
			mi->tera_consecutive++;
			mi->tera_time_interval = 100;
			///printk (KERN_DEBUG "reset 100ms\n");
		}
		else {
			mi->tera_consecutive = 0;
		}
		
 		//printk(KERN_DEBUG "last tp better %2d avg_tp %2d cur tp %2d curate=%2d last rate=%2d tera_consecutive %2d multipli %2d\n", mi->last_tp, mi->tera_avg_tp, mr->cur_tp, mi->cur_rateidx, mi->last_rateidx, mi->tera_consecutive, mi->isMultiplicative);
		mi->stats_update_adaptive = jiffies;
		
		return;
	}


	if (mi->tera_tx_ok + mi->tera_tx_err > 0 && mr->cur_tp >= 0) 
		mi->tera_avg_tp = tera_ewma (mi->tera_avg_tp, mr->cur_tp, 75); 

	if (mi->tera_avg_tp == 0 )  {
		return;
	}

	if (mr->cur_tp >= mi->tera_avg_tp ) {
		mi->tera_successive = 0;

                if (rate + 1 <  mi->n_rates && !mi->isMultiplicative && !mi->tera_ossilate) {
                        rate++;
			mi->isProbing = true;
			//printk(KERN_DEBUG " increase additively\n");
                
                } else if (rate + 1 <  mi->n_rates && mi->isMultiplicative && !mi->tera_ossilate) {
			if (rate + rate < mi->n_rates)
				rate = rate + rate;
			else
				rate = mi->n_rates - 1;
			mi->isProbing = true;
			//printk(KERN_DEBUG " increase multiplicatively\n");
		}

	} else if (mr->cur_tp  <=  (mi->tera_avg_tp * 90)/100  && mr->cur_tp >=  (mi->tera_avg_tp * 75)/100) {
		if (rate > 0) {
			rate--;
			//printk(KERN_DEBUG " decrease additively\n");
		}
		mi->tera_consecutive = 0;
		mi->tera_ossilate = 0;
	} else if (mr->cur_tp <  (mi->tera_avg_tp * 75)/100 ) {
		mi->tera_successive++;
		if (rate >= 1 && mi->tera_successive > 1)
			rate = rate *3/4;
		else if (rate > 0 && mi->tera_successive == 1)
			rate--;
		mi->tera_consecutive = 0;
		mi->tera_ossilate = 0;
	}

	mi->last_tp = mr->cur_tp;
	mi->last_rateidx = mi->cur_rateidx;
		
	
	if ( rate != mi->cur_rateidx) {
 		//printk(KERN_DEBUG ">>>>>>set cur_rateidx=%2d to rate=%2d\n", mi->cur_rateidx, rate);
		mi->cur_rateidx = rate;
		mi->tera_tx_ok = mi->tera_tx_err = mi->tera_tx_retr = mi->tera_tx_credit = 0;
	}
}

static void
tera_update_stats(struct tera_priv *mp, struct tera_sta_info *mi)
{
	u32 max_tp = 0, index_max_tp = 0, index_max_tp2 = 0;
	u32 max_prob = 0, index_max_prob = 0;
	u32 usecs;
	u32 p;
	int i;


	mi->stats_update = jiffies;
	for (i = 0; i < mi->n_rates; i++) {
		struct tera_rate *mr = &mi->r[i];

		usecs = mr->perfect_tx_time;
		if (!usecs)
			usecs = 1000000;

		/* To avoid rounding issues, probabilities scale from 0 (0%)
		 * to 18000 (100%) */
		if (mr->attempts) {
			p = (mr->success * 18000) / mr->attempts;
			mr->succ_hist += mr->success;
			mr->att_hist += mr->attempts;
			mr->cur_prob = p;
			p = ((p * (100 - mp->ewma_level)) + (mr->probability *
				mp->ewma_level)) / 100;
			mr->probability = p;
			mr->cur_tp = p * (1000000 / usecs);
		}

		mr->last_success = mr->success;
		mr->last_attempts = mr->attempts;
		mr->success = 0;
		mr->attempts = 0;

		
		/* less often below the 10% chance of success.
		 * less often above the 95% chance of success. */
		if ((mr->probability > 17100) || (mr->probability < 1800)) {
			mr->adjusted_retry_count = mr->retry_count >> 1;
			if (mr->adjusted_retry_count > 2)
				mr->adjusted_retry_count = 2;
		} else {
			mr->adjusted_retry_count = mr->retry_count;
		}
		if (!mr->adjusted_retry_count)
			mr->adjusted_retry_count = 2;
	}

	for (i = 0; i < mi->n_rates; i++) {
		struct tera_rate *mr = &mi->r[i];
		if (max_tp < mr->cur_tp) {
			index_max_tp = i;
			max_tp = mr->cur_tp;
		}
		if (max_prob < mr->probability) {
			index_max_prob = i;
			max_prob = mr->probability;
		}
	}

	max_tp = 0;
	for (i = 0; i < mi->n_rates; i++) {
		struct tera_rate *mr = &mi->r[i];

		if (i == index_max_tp)
			continue;

		if (max_tp < mr->cur_tp) {
			index_max_tp2 = i;
			max_tp = mr->cur_tp;
		}
	}


	mi->max_tp_rate = index_max_tp;
	mi->max_tp_rate2 = index_max_tp2;
	mi->max_prob_rate = index_max_prob;

	tera_check_rate (mi);

}
static void
tera_tx_status(void *priv, struct ieee80211_supported_band *sband,
                   struct ieee80211_sta *sta, void *priv_sta,
		   struct sk_buff *skb)
{
	struct tera_sta_info *mi = priv_sta;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *ar = info->status.rates;
	int i, ndx;
	int success;


	success = !!(info->flags & IEEE80211_TX_STAT_ACK);

	for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
		if (ar[i].idx < 0)
			break;

		ndx = rix_to_ndx(mi, ar[i].idx);
		if (ndx < 0)
			continue;

		mi->r[ndx].attempts += ar[i].count;

		if ((i != IEEE80211_TX_MAX_RATES - 1) && (ar[i + 1].idx < 0)) {
		  
			mi->r[ndx].success += success;

		}
	}

	if(!mi) return;

	//ignore other frames
	if (info->status.rates[0].idx != mi->cur_rateidx) {
		//printk(KERN_DEBUG "tera_tx_stats idx=%2d != cur_rateidx=%2d", info->status.rates[0].idx, mi->cur_rateidx);
		return;
	}

	mi->tera_tx_ok += success;

	if (!(info->flags & IEEE80211_TX_STAT_ACK)) {
		mi->tera_tx_err +=2;
	}
	//frames that got some retries but made it out
	else if (info->status.rates[0].count > 1) {
		mi->tera_tx_err++;
	}
		mi->tera_tx_retr += ar[0].count;



 	//printk(KERN_DEBUG "tera_tx_status rate=%2d err=%2d ok=%2d retr=%2d suc=%2d\n", mi->cur_rateidx, mi->tera_tx_err,mi->tera_tx_ok, mi->tera_tx_retr,success);
}


static inline unsigned int
tera_get_retry_count(struct tera_rate *mr,
                         struct ieee80211_tx_info *info)
{
	unsigned int retry = mr->adjusted_retry_count;

	if (info->control.rates[0].flags & IEEE80211_TX_RC_USE_RTS_CTS)
		retry = max(2U, min(mr->retry_count_rtscts, retry));
	else if (info->control.rates[0].flags & IEEE80211_TX_RC_USE_CTS_PROTECT)
		retry = max(2U, min(mr->retry_count_cts, retry));
	return retry;
}

static void
tera_get_rate(void *priv, struct ieee80211_sta *sta,
		  void *priv_sta, struct ieee80211_tx_rate_control *txrc)
{
	struct sk_buff *skb = txrc->skb;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct tera_sta_info *mi = priv_sta;
	struct tera_priv *mp = priv;
	struct ieee80211_tx_rate *ar = info->control.rates;
	unsigned int ndx;
	bool mrr;
	int i;
	int mrr_ndx[3];
	int rate;

	if (rate_control_send_low(sta, priv_sta, txrc))
		return;

	mrr = mp->has_mrr && !txrc->rts && !txrc->bss_conf->use_cts_prot;

	if (time_after(jiffies, mi->stats_update_adaptive + (mi->tera_time_interval *
			HZ) / 1000)) {
		printk (KERN_DEBUG "reset ossilate\n");
		mi->tera_ossilate = 0;
		mi->stats_update_adaptive = jiffies;
	}
	
	if (time_after(jiffies, mi->stats_update + (mp->update_interval *
			HZ) / 1000)) {
		tera_update_stats(mp, mi);
        	tera_check_rate (mi);
	}


	ndx = mi->cur_rateidx;

	mi->packet_count++;

	ar[0].idx = mi->r[ndx].rix;
	ar[0].count = tera_get_retry_count(&mi->r[ndx], info);

	if (!mrr) {
		ar[0].count = mp->max_retry;
		ar[1].idx = mi->lowest_rix;
		ar[1].count = mp->max_retry;
		return;
	}



	//MRR Setup
	rate = ndx;

	//second retry 
	if ( --rate >= 0)
		mrr_ndx[0] = rate; 
        else 
		mrr_ndx[0] = 0; 

 	//third retry
        if (rate > 2) rate = (int) rate/2;
        else rate--;

	if( rate >= 0) 
		mrr_ndx[1] = rate;
	else 
		mrr_ndx[1] = 0;

	//base rate
	mrr_ndx[2] = 0;

	for (i = 1; i < 4; i++) {
		ar[i].idx = mi->r[mrr_ndx[i - 1]].rix;
		ar[i].count = mi->r[mrr_ndx[i - 1]].adjusted_retry_count;
	}

}


static void
calc_rate_durations(struct tera_sta_info *mi, struct ieee80211_local *local,
                    struct tera_rate *d, struct ieee80211_rate *rate)
{
	int erp = !!(rate->flags & IEEE80211_RATE_ERP_G);

	d->perfect_tx_time = ieee80211_frame_duration(local, 1200,
			rate->bitrate, erp, 1);
	d->ack_time = ieee80211_frame_duration(local, 10,
			rate->bitrate, erp, 1);
}

static void
tera_rate_init(void *priv, struct ieee80211_supported_band *sband,
               struct ieee80211_sta *sta, void *priv_sta)
{
	struct tera_sta_info *mi = priv_sta;
	struct tera_priv *mp = priv;
	struct ieee80211_local *local = hw_to_local(mp->hw);
	struct ieee80211_rate *ctl_rate;
	unsigned int i, n = 0;
	unsigned int t_slot = 9; /* FIXME: get real slot time */

	mi->cur_rateidx = mi->lowest_rix = rate_lowest_index(sband, sta);
	mi->tera_tx_ok = mi->tera_tx_err = mi->tera_tx_retr = mi->tera_tx_credit = 0;
	mi->tera_avg_tp = mi->tera_ossilate = mi->tera_successive =  mi->tera_consecutive = 0;
	mi->tera_time_interval = 100;
	mi->isMultiplicative = false;
	mi->isProbing = false;
	mi->last_tp = 0;

	ctl_rate = &sband->bitrates[mi->lowest_rix];
	mi->sp_ack_dur = ieee80211_frame_duration(local, 10, ctl_rate->bitrate,
				!!(ctl_rate->flags & IEEE80211_RATE_ERP_G), 1);

	for (i = 0; i < sband->n_bitrates; i++) {
		struct tera_rate *mr = &mi->r[n];
		unsigned int tx_time = 0, tx_time_cts = 0, tx_time_rtscts = 0;
		unsigned int tx_time_single;
		unsigned int cw = mp->cw_min;

		if (!rate_supported(sta, sband->band, i))
			continue;
		n++;
		memset(mr, 0, sizeof(*mr));

		mr->rix = i;
		mr->bitrate = sband->bitrates[i].bitrate / 5;
		calc_rate_durations(mi, local, mr,
				&sband->bitrates[i]);

		/* calculate maximum number of retransmissions before
		 * fallback (based on maximum segment size) */
		mr->retry_count = 1;
		mr->retry_count_cts = 1;
		mr->retry_count_rtscts = 1;
		tx_time = mr->perfect_tx_time + mi->sp_ack_dur;
		do {
			/* add one retransmission */
			tx_time_single = mr->ack_time + mr->perfect_tx_time;

			/* contention window */
			tx_time_single += t_slot + min(cw, mp->cw_max);
			cw = (cw << 1) | 1;

			tx_time += tx_time_single;
			tx_time_cts += tx_time_single + mi->sp_ack_dur;
			tx_time_rtscts += tx_time_single + 2 * mi->sp_ack_dur;
			if ((tx_time_cts < mp->segment_size) &&
				(mr->retry_count_cts < mp->max_retry))
				mr->retry_count_cts++;
			if ((tx_time_rtscts < mp->segment_size) &&
				(mr->retry_count_rtscts < mp->max_retry))
				mr->retry_count_rtscts++;
		} while ((tx_time < mp->segment_size) &&
				(++mr->retry_count < mp->max_retry));
		mr->adjusted_retry_count = mr->retry_count;
	}

	for (i = n; i < sband->n_bitrates; i++) {
		struct tera_rate *mr = &mi->r[i];
		mr->rix = -1;
	}

	mi->n_rates = n;
	mi->stats_update = jiffies;
	mi->stats_update_adaptive = jiffies;

}

static void *
tera_alloc_sta(void *priv, struct ieee80211_sta *sta, gfp_t gfp)
{
	struct ieee80211_supported_band *sband;
	struct tera_sta_info *mi;
	struct tera_priv *mp = priv;
	struct ieee80211_hw *hw = mp->hw;
	int max_rates = 0;
	int i;

	mi = kzalloc(sizeof(struct tera_sta_info), gfp);
	if (!mi)
		return NULL;

	for (i = 0; i < IEEE80211_NUM_BANDS; i++) {
		sband = hw->wiphy->bands[i];
		if (sband && sband->n_bitrates > max_rates)
			max_rates = sband->n_bitrates;
	}

	mi->r = kzalloc(sizeof(struct tera_rate) * max_rates, gfp);
	if (!mi->r)
		goto error;

	mi->stats_update = jiffies;
	mi->stats_update_adaptive = jiffies;
	return mi;

error:
	kfree(mi);
	return NULL;
}

static void
tera_free_sta(void *priv, struct ieee80211_sta *sta, void *priv_sta)
{
	struct tera_sta_info *mi = priv_sta;

	kfree(mi->r);
	kfree(mi);
}

static void *
tera_alloc(struct ieee80211_hw *hw, struct dentry *debugfsdir)
{
	struct tera_priv *mp;

	mp = kzalloc(sizeof(struct tera_priv), GFP_ATOMIC);
	if (!mp)
		return NULL;

	/* contention window settings
	 * Just an approximation. Using the per-queue values would complicate
	 * the calculations and is probably unnecessary */
	mp->cw_min = 15;
	mp->cw_max = 1023;

	/* moving average weight for EWMA */
	mp->ewma_level = 75;

	/* maximum time that the hw is allowed to stay in one MRR segment */
	mp->segment_size = 6000;

	if (hw->max_rate_tries > 0)
		mp->max_retry = hw->max_rate_tries;
	else
		/* safe default, does not necessarily have to match hw properties */
		mp->max_retry = 7;

	if (hw->max_rates >= 4)
		mp->has_mrr = true;

	mp->hw = hw;
	mp->update_interval = 100;

	mp->ath_rate_raise = 10;

	return mp;
}

static void
tera_free(void *priv)
{
	kfree(priv);
}

static struct rate_control_ops mac80211_tera = {
	.name = "tera",
	.tx_status = tera_tx_status,
	.get_rate = tera_get_rate,
	.rate_init = tera_rate_init,
	.alloc = tera_alloc,
	.free = tera_free,
	.alloc_sta = tera_alloc_sta,
	.free_sta = tera_free_sta,
#ifdef CONFIG_MAC80211_DEBUGFS
	.add_sta_debugfs = tera_add_sta_debugfs,
	.remove_sta_debugfs = tera_remove_sta_debugfs,
#endif
};

int __init
rc80211_tera_init(void)
{
	return ieee80211_rate_control_register(&mac80211_tera);
}

void
rc80211_tera_exit(void)
{
	ieee80211_rate_control_unregister(&mac80211_tera);
}

