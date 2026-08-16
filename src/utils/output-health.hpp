#pragma once

#include <obs.h>

struct OutputHealthStats {
	int dropped_frames = 0;
	int total_frames = 0;
	double dropped_percentage = 0.0;
	double congestion_percentage = 0.0;
};

inline OutputHealthStats GetOutputHealthStats(obs_output_t *output)
{
	if (!output)
		return {};

	const auto droppedFrames = obs_output_get_frames_dropped(output);
	const auto totalFrames = obs_output_get_total_frames(output);
	const auto droppedPercentage =
		totalFrames > 0 ? (double)droppedFrames / (double)totalFrames * 100.0 : 0.0; // OBS total frames already includes dropped attempts, matching its own status bar and stop log. (Codex task: 019ff120-ea11-71a3-8b65-c55b45cac2fe)
	return {droppedFrames, totalFrames, droppedPercentage, (double)obs_output_get_congestion(output) * 100.0};
}
