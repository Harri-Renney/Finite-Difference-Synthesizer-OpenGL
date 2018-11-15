#include <vector>

//Add adjustable duration//
class SineWaveExcitor {
private:
	int index = 0;
	std::vector<float> sineWave;
public:
	SineWaveExcitor();
	float getNextSample();
	void resetExcitation();
	bool isExcitation();
};