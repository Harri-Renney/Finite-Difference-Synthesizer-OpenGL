#include <vector>

class SquareWaveExcitor {
private:
	int index = 0;
	std::vector<float> squareWave;
public:
	SquareWaveExcitor();
	float getNextSample();
	void resetExcitation();
	bool isExcitation();
};