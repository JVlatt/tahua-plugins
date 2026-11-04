#pragma once
#include <TahuaPlugin.h>

class Waves : public ffglqs::Source
{
public:
	Waves();

	void Update() override;

private:
	std::shared_ptr< ffglqs::ParamBool > sync;
	std::shared_ptr< ffglqs::ParamOption > beats;
	std::shared_ptr< ffglqs::ParamRange > speed;

	double cycles      = 0.0;// Animation cycles elapsed, free-running or derived from the bar count.
	double bars        = 0.0;// Bars seen since the plugin started, counted on each barPhase wrap.
	float lastBarPhase = 0.0f;
};
