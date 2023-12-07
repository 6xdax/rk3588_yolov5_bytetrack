#include "BYTETracker.h"
#include <fstream>

static double SPEED_MAX = 2;
BYTETracker::BYTETracker(int frame_rate, int track_buffer)
{
	track_thresh = 0.5;
	high_thresh = 0.6;
	match_thresh = 0.8;

	frame_id = 0;
	max_time_lost = int(frame_rate / 30.0 * track_buffer);
	cout << "Init ByteTrack!" << endl;
}

BYTETracker::~BYTETracker()
{
}

vector<STrack> BYTETracker::update(const vector<Object>& objects, int fps, long long num_frames)
{

	////////////////// Step 1: Get detections //////////////////
	this->frame_id++;
	vector<STrack> activated_stracks;
	vector<STrack> refind_stracks;
	vector<STrack> removed_stracks;
	vector<STrack> lost_stracks;
	vector<STrack> detections;
	vector<STrack> detections_low;

	vector<STrack> detections_cp;
	vector<STrack> tracked_stracks_swap;
	vector<STrack> resa, resb;
	vector<STrack> output_stracks;

	vector<STrack*> unconfirmed;
	vector<STrack*> tracked_stracks;
	vector<STrack*> strack_pool;
	vector<STrack*> r_tracked_stracks;

	if (objects.size() > 0)
	{
		// 获取所有的框
		// omp_set_num_threads(8);
   		// #pragma omp parallel for
		for (int i = 0; i < objects.size(); i++)
		{
			vector<float> tlbr_;
			tlbr_.resize(4);
			tlbr_[0] = objects[i].rect.x;
			tlbr_[1] = objects[i].rect.y;
			tlbr_[2] = objects[i].rect.x + objects[i].rect.width;
			tlbr_[3] = objects[i].rect.y + objects[i].rect.height;

			float score = objects[i].prob;

			STrack strack(STrack::tlbr_to_tlwh(tlbr_), score, objects[i].name, objects[i].label);
			if (score >= track_thresh)
			{
				detections.push_back(strack);
			}
			else
			{
				detections_low.push_back(strack);
			}
			
		}
	}

	// Add newly detected tracklets to tracked_stracks
	for (int i = 0; i < this->tracked_stracks.size(); i++)
	{
		if (!this->tracked_stracks[i].is_activated)
			unconfirmed.push_back(&this->tracked_stracks[i]);
		else
			tracked_stracks.push_back(&this->tracked_stracks[i]);
	}

	////////////////// Step 2: First association, with IoU //////////////////
	strack_pool = joint_stracks(tracked_stracks, this->lost_stracks);
	STrack::multi_predict(strack_pool, this->kalman_filter);

	vector<vector<float> > dists;
	int dist_size = 0, dist_size_size = 0;
	dists = iou_distance(strack_pool, detections, dist_size, dist_size_size);

	vector<vector<int> > matches;
	vector<int> u_track, u_detection;
	linear_assignment(dists, dist_size, dist_size_size, match_thresh, matches, u_track, u_detection);

	for (int i = 0; i < matches.size(); i++)
	{
		STrack *track = strack_pool[matches[i][0]];
		STrack *det = &detections[matches[i][1]];
		if (track->state == TrackState::Tracked)
		{
			track->update(*det, this->frame_id);
			activated_stracks.push_back(*track);
		}
		else
		{
			track->re_activate(*det, this->frame_id, false);
			refind_stracks.push_back(*track);
		}
	}

	////////////////// Step 3: Second association, using low score dets //////////////////
	for (int i = 0; i < u_detection.size(); i++)
	{
		detections_cp.push_back(detections[u_detection[i]]);
	}
	detections.clear();
	detections.assign(detections_low.begin(), detections_low.end());
	
	for (int i = 0; i < u_track.size(); i++)
	{
		if (strack_pool[u_track[i]]->state == TrackState::Tracked)
		{
			r_tracked_stracks.push_back(strack_pool[u_track[i]]);
		}
	}

	dists.clear();
	dists = iou_distance(r_tracked_stracks, detections, dist_size, dist_size_size);

	matches.clear();
	u_track.clear();
	u_detection.clear();
	linear_assignment(dists, dist_size, dist_size_size, 0.5, matches, u_track, u_detection);

	for (int i = 0; i < matches.size(); i++)
	{
		STrack *track = r_tracked_stracks[matches[i][0]];
		STrack *det = &detections[matches[i][1]];
		if (track->state == TrackState::Tracked)
		{
			track->update(*det, this->frame_id);
			activated_stracks.push_back(*track);
		}
		else
		{
			track->re_activate(*det, this->frame_id, false);
			refind_stracks.push_back(*track);
		}
	}

	for (int i = 0; i < u_track.size(); i++)
	{
		STrack *track = r_tracked_stracks[u_track[i]];
		if (track->state != TrackState::Lost)
		{
			track->mark_lost();
			lost_stracks.push_back(*track);
		}
	}

	// Deal with unconfirmed tracks, usually tracks with only one beginning frame
	detections.clear();
	detections.assign(detections_cp.begin(), detections_cp.end());

	dists.clear();
	dists = iou_distance(unconfirmed, detections, dist_size, dist_size_size);

	matches.clear();
	vector<int> u_unconfirmed;
	u_detection.clear();
	linear_assignment(dists, dist_size, dist_size_size, 0.7, matches, u_unconfirmed, u_detection);

	for (int i = 0; i < matches.size(); i++)
	{
		unconfirmed[matches[i][0]]->update(detections[matches[i][1]], this->frame_id);
		activated_stracks.push_back(*unconfirmed[matches[i][0]]);
	}

	for (int i = 0; i < u_unconfirmed.size(); i++)
	{
		STrack *track = unconfirmed[u_unconfirmed[i]];
		track->mark_removed();
		removed_stracks.push_back(*track);
	}

	////////////////// Step 4: Init new stracks //////////////////
	for (int i = 0; i < u_detection.size(); i++)
	{
		STrack *track = &detections[u_detection[i]];
		if (track->score < this->high_thresh)
			continue;
		track->activate(this->kalman_filter, this->frame_id);
		activated_stracks.push_back(*track);
	}

	////////////////// Step 5: Update state //////////////////
	for (int i = 0; i < this->lost_stracks.size(); i++)
	{
		if (this->frame_id - this->lost_stracks[i].end_frame() > this->max_time_lost)
		{
			this->lost_stracks[i].mark_removed();
			removed_stracks.push_back(this->lost_stracks[i]);
		}
	}
	
	for (int i = 0; i < this->tracked_stracks.size(); i++)
	{
		if (this->tracked_stracks[i].state == TrackState::Tracked)
		{
			tracked_stracks_swap.push_back(this->tracked_stracks[i]);
		}
	}
	this->tracked_stracks.clear();
	this->tracked_stracks.assign(tracked_stracks_swap.begin(), tracked_stracks_swap.end());

	this->tracked_stracks = joint_stracks(this->tracked_stracks, activated_stracks);
	this->tracked_stracks = joint_stracks(this->tracked_stracks, refind_stracks);

	//std::cout << activated_stracks.size() << std::endl;

	this->lost_stracks = sub_stracks(this->lost_stracks, this->tracked_stracks);
	for (int i = 0; i < lost_stracks.size(); i++)
	{
		this->lost_stracks.push_back(lost_stracks[i]);
	}

	this->lost_stracks = sub_stracks(this->lost_stracks, this->removed_stracks);
	for (int i = 0; i < removed_stracks.size(); i++)
	{
		this->removed_stracks.push_back(removed_stracks[i]);
	}
	
	remove_duplicate_stracks(resa, resb, this->tracked_stracks, this->lost_stracks);

	this->tracked_stracks.clear();
	this->tracked_stracks.assign(resa.begin(), resa.end());
	this->lost_stracks.clear();
	this->lost_stracks.assign(resb.begin(), resb.end());

	for (int i = 0; i < this->tracked_stracks.size(); i++)
	{
		if (this->tracked_stracks[i].is_activated)
		{
			if (num_frames == 1)
			{
				// 中心点
				vector<float> tlwh = this->tracked_stracks[i].tlwh;
				this->tracked_stracks[i].last_center.x = int(tlwh[0]+tlwh[2]/2);
				this->tracked_stracks[i].last_center.y = int(tlwh[1]+2*tlwh[3]/3);
			}
			// 速度计算
			if (num_frames % fps ==0)
			{
				// 中心点
				vector<float> tlwh = this->tracked_stracks[i].tlwh;
				this->tracked_stracks[i].now_center.x = int(tlwh[0]+tlwh[2]/2);
				this->tracked_stracks[i].now_center.y = int(tlwh[1]+2*tlwh[3]/3);

				// 速度
				double speed_x,speed_y;
				double speed;
				double coefficient = 1;	//透视系数
				speed_x = coefficient * abs(this->tracked_stracks[i].now_center.x - this->tracked_stracks[i].last_center.x);
				speed_y = coefficient * abs(this->tracked_stracks[i].now_center.y - this->tracked_stracks[i].last_center.y);
				speed = sqrt(speed_x*speed_x + speed_y*speed_y);
				this->tracked_stracks[i].speed_1s = speed;
			    // 每秒速度累加
				this->tracked_stracks[i].speed_all += this->tracked_stracks[i].speed_1s;
				// 中心点更新
				this->tracked_stracks[i].last_center.x = this->tracked_stracks[i].now_center.x;
				this->tracked_stracks[i].last_center.y = this->tracked_stracks[i].now_center.y;
				
				// 更新每分钟速度
				if(num_frames % fps == 0)
				{
					this->tracked_stracks[i].speed_1m = this->tracked_stracks[i].speed_all / fps;
					if (this->tracked_stracks[i].speed_1m<0.5)
					{
						this->tracked_stracks[i].speed_1m = 0;
					}
					this->tracked_stracks[i].speed_all = 0;
					// if(this->tracked_stracks[i].speed_1m > SPEED_MAX)
					// {
					// 	SPEED_MAX= this->tracked_stracks[i].speed_1m;
					// cout<< this->tracked_stracks[i].speed_1m <<endl;
					// }
					this->tracked_stracks[i].liveness = LINENESS_PROP*this->tracked_stracks[i].liveness+
					(1-LINENESS_PROP)*(POSTURE_PROP*this->tracked_stracks[i].label+SPEED_PROP*this->tracked_stracks[i].speed_1m/SPEED_MAX);
					if(this->tracked_stracks[i].liveness > 1)
					{
						this->tracked_stracks[i].liveness=1;
					}
				}
			}
			output_stracks.push_back(this->tracked_stracks[i]);
		}
	}
	return output_stracks;
}