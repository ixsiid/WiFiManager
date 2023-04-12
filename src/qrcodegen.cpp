/*
 * QR Code generator library (C++)
 *
 * Copyright (c) Project Nayuki. (MIT License)
 * https://www.nayuki.io/page/qr-code-generator-library
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * - The above copyright notice and this permission notice shall be included in
 *   all copies or substantial portions of the Software.
 * - The Software is provided "as is", without warranty of any kind, express or
 *   implied, including but not limited to the warranties of merchantability,
 *   fitness for a particular purpose and noninfringement. In no event shall the
 *   authors or copyright holders be liable for any claim, damages or other
 *   liability, whether in an action of contract, tort or otherwise, arising from,
 *   out of or in connection with the Software or the use or other dealings in the
 *   Software.
 */

#include <algorithm>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <utility>
#include "qrcodegen.hpp"

#include <esp_log.h>

using std::int8_t;
using std::size_t;
using std::uint8_t;
using std::vector;

namespace qrcodegen {

/*---- Class QrSegment ----*/

QrSegment::Mode::Mode(int mode, int cc0, int cc1, int cc2) : modeBits(mode) {
	numBitsCharCount[0] = cc0;
	numBitsCharCount[1] = cc1;
	numBitsCharCount[2] = cc2;
}

int QrSegment::Mode::getModeBits() const {
	return modeBits;
}

int QrSegment::Mode::numCharCountBits() const {
	return numBitsCharCount[(QrCode::version + 7) / 17];
}

const QrSegment::Mode QrSegment::Mode::BYTE(0x4, 8, 16, 16);

QrSegment QrSegment::makeBytes(const vector<uint8_t> &data) {
	// if (data.size() > static_cast<unsigned int>(INT_MAX))
	// 	throw std::length_error("Data too long");
	BitBuffer bb;
	for (uint8_t b : data) bb.appendBits(b, 8);
	return QrSegment(Mode::BYTE, static_cast<int>(data.size()), std::move(bb));
}

vector<QrSegment> QrSegment::makeSegments(const char *text) {
	// Select the most efficient segment encoding automatically
	vector<QrSegment> result;

	vector<uint8_t> bytes;
	for (; *text != '\0'; text++) bytes.push_back(static_cast<uint8_t>(*text));
	result.push_back(makeBytes(bytes));

	return result;
}

QrSegment::QrSegment(const Mode &md, int numCh, const std::vector<bool> &dt) : mode(&md),
															    numChars(numCh),
															    data(dt) {
	// if (numCh < 0)
	//	throw std::domain_error("Invalid value");
}

int QrSegment::getTotalBits(const vector<QrSegment> &segs) {
	int result = 0;
	for (const QrSegment &seg : segs) {
		int ccbits = seg.mode->numCharCountBits();
		if (seg.numChars >= (1L << ccbits))
			return -1;  // The segment's length doesn't fit the field's bit width
		if (4 + ccbits > INT_MAX - result)
			return -1;  // The sum will overflow an int type
		result += 4 + ccbits;
		if (seg.data.size() > static_cast<unsigned int>(INT_MAX - result))
			return -1;  // The sum will overflow an int type
		result += static_cast<int>(seg.data.size());
	}
	return result;
}

const QrSegment::Mode &QrSegment::getMode() const {
	return *mode;
}

int QrSegment::getNumChars() const {
	return numChars;
}

const std::vector<bool> &QrSegment::getData() const {
	return data;
}

QrCode QrCode::encodeText(const char *text) {
	vector<QrSegment> segs = QrSegment::makeSegments(text);

	int mask = -1;

	int dataUsedBits = QrSegment::getTotalBits(segs);
	assert(dataUsedBits != -1);

	// Concatenate all segments to create the data bit string
	BitBuffer bb;
	for (const QrSegment &seg : segs) {
		bb.appendBits(static_cast<uint32_t>(seg.getMode().getModeBits()), 4);
		bb.appendBits(static_cast<uint32_t>(seg.getNumChars()), seg.getMode().numCharCountBits());
		bb.insert(bb.end(), seg.getData().begin(), seg.getData().end());
	}
	assert(bb.size() == static_cast<unsigned int>(dataUsedBits));

	// Add terminator and pad up to a byte if applicable
	size_t dataCapacityBits = static_cast<size_t>(getNumDataCodewords()) * 8;
	assert(bb.size() <= dataCapacityBits);
	bb.appendBits(0, std::min(4, static_cast<int>(dataCapacityBits - bb.size())));
	bb.appendBits(0, (8 - static_cast<int>(bb.size() % 8)) % 8);
	assert(bb.size() % 8 == 0);

	// Pad with alternating bytes until data capacity is reached
	for (uint8_t padByte = 0xEC; bb.size() < dataCapacityBits; padByte ^= 0xEC ^ 0x11)
		bb.appendBits(padByte, 8);

	// Pack bits into bytes in big endian
	vector<uint8_t> dataCodewords(bb.size() / 8);
	for (size_t i = 0; i < bb.size(); i++)
		dataCodewords.at(i >> 3) |= (bb.at(i) ? 1 : 0) << (7 - (i & 7));

	ESP_LOG_BUFFER_HEXDUMP("a", dataCodewords.data(), dataCodewords.size(), ESP_LOG_INFO);
	ESP_LOG_BUFFER_HEXDUMP("a", text, strlen(text), ESP_LOG_INFO);

	vector<uint8_t> bb8; // its same with dataCodewords
	for (int i = 0; i < bb.size() / 8; i++) {
			bb8.push_back(
		    bb[i * 8 + 0] << 7 |
		    bb[i * 8 + 1] << 6 |
		    bb[i * 8 + 2] << 5 |
		    bb[i * 8 + 3] << 4 |
		    bb[i * 8 + 4] << 3 |
		    bb[i * 8 + 5] << 2 |
		    bb[i * 8 + 6] << 1 |
		    bb[i * 8 + 7] << 0);
	}
	ESP_LOG_BUFFER_HEXDUMP("a", bb8.data(), bb8.size(), ESP_LOG_INFO);
	for (int i=0; i<bb.size(); i++) {
		if (i % 41 == 0) printf("\n");
		printf("%c", bb[i] ? '0' : ' ');
	}
	printf("\n");

	// Create the QR Code object
	return QrCode(dataCodewords, mask);
}

QrCode::QrCode(const vector<uint8_t> &dataCodewords, int msk) {
	size_t sz	 = static_cast<size_t>(size);
	modules	 = vector<vector<bool> >(sz, vector<bool>(sz));  // Initially all light
	isFunction = vector<vector<bool> >(sz, vector<bool>(sz));

	// Compute ECC, draw modules
	drawFunctionPatterns();
	const vector<uint8_t> allCodewords = addEccAndInterleave(dataCodewords);
	drawCodewords(allCodewords);

	// Do masking
	if (msk == -1) {  // Automatically choose best mask
		long minPenalty = LONG_MAX;
		for (int i = 0; i < 8; i++) {
			applyMask(i);
			drawFormatBits(i);
			long penalty = getPenaltyScore();
			if (penalty < minPenalty) {
				msk		 = i;
				minPenalty = penalty;
			}
			applyMask(i);	// Undoes the mask due to XOR
		}
	}
	assert(0 <= msk && msk <= 7);
	mask = msk;
	applyMask(msk);	  // Apply the final choice of mask
	drawFormatBits(msk);  // Overwrite old format bits

	isFunction.clear();
	isFunction.shrink_to_fit();
}

int QrCode::getMask() const {
	return mask;
}

bool QrCode::getModule(int x, int y) const {
	return 0 <= x && x < size && 0 <= y && y < size && module(x, y);
}

void QrCode::drawFunctionPatterns() {
	// Draw horizontal and vertical timing patterns
	for (int i = 0; i < size; i++) {
		setFunctionModule(6, i, i % 2 == 0);
		setFunctionModule(i, 6, i % 2 == 0);
	}

	// Draw 3 finder patterns (all corners except bottom right; overwrites some timing modules)
	drawFinderPattern(3, 3);
	drawFinderPattern(size - 4, 3);
	drawFinderPattern(3, size - 4);

	// Draw numerous alignment patterns
	const vector<int> alignPatPos = getAlignmentPatternPositions();
	size_t numAlign			= alignPatPos.size();
	for (size_t i = 0; i < numAlign; i++) {
		for (size_t j = 0; j < numAlign; j++) {
			// Don't draw on the three finder corners
			if (!((i == 0 && j == 0) || (i == 0 && j == numAlign - 1) || (i == numAlign - 1 && j == 0)))
				drawAlignmentPattern(alignPatPos.at(i), alignPatPos.at(j));
		}
	}

	// Draw configuration data
	drawFormatBits(0);	// Dummy mask value; overwritten later in the constructor
}

void QrCode::drawFormatBits(int msk) {
	// Calculate error correction code and pack bits
	// int data = getFormatBits(errorCorrectionLevel) << 3 | msk;	// errCorrLvl is uint2, msk is uint3
	int data = 1 << 3 | msk;
	int rem  = data;
	for (int i = 0; i < 10; i++)
		rem = (rem << 1) ^ ((rem >> 9) * 0x537);
	int bits = (data << 10 | rem) ^ 0x5412;	 // uint15
	assert(bits >> 15 == 0);

	// Draw first copy
	for (int i = 0; i <= 5; i++)
		setFunctionModule(8, i, getBit(bits, i));
	setFunctionModule(8, 7, getBit(bits, 6));
	setFunctionModule(8, 8, getBit(bits, 7));
	setFunctionModule(7, 8, getBit(bits, 8));
	for (int i = 9; i < 15; i++)
		setFunctionModule(14 - i, 8, getBit(bits, i));

	// Draw second copy
	for (int i = 0; i < 8; i++)
		setFunctionModule(size - 1 - i, 8, getBit(bits, i));
	for (int i = 8; i < 15; i++)
		setFunctionModule(8, size - 15 + i, getBit(bits, i));
	setFunctionModule(8, size - 8, true);  // Always dark
}

void QrCode::drawFinderPattern(int x, int y) {
	for (int dy = -4; dy <= 4; dy++) {
		for (int dx = -4; dx <= 4; dx++) {
			int dist = std::max(std::abs(dx), std::abs(dy));	// Chebyshev/infinity norm
			int xx = x + dx, yy = y + dy;
			if (0 <= xx && xx < size && 0 <= yy && yy < size)
				setFunctionModule(xx, yy, dist != 2 && dist != 4);
		}
	}
}

void QrCode::drawAlignmentPattern(int x, int y) {
	for (int dy = -2; dy <= 2; dy++) {
		for (int dx = -2; dx <= 2; dx++)
			setFunctionModule(x + dx, y + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
	}
}

void QrCode::setFunctionModule(int x, int y, bool isDark) {
	size_t ux				= static_cast<size_t>(x);
	size_t uy				= static_cast<size_t>(y);
	modules.at(uy).at(ux)	= isDark;
	isFunction.at(uy).at(ux) = true;
}

bool QrCode::module(int x, int y) const {
	return modules.at(static_cast<size_t>(y)).at(static_cast<size_t>(x));
}

vector<uint8_t> QrCode::addEccAndInterleave(const vector<uint8_t> &data) const {
	// Calculate parameter numbers
	int numBlocks	    = NUM_ERROR_CORRECTION_BLOCKS;
	int blockEccLen    = ECC_CODEWORDS_PER_BLOCK;
	int rawCodewords   = getNumRawDataModules() / 8;
	int numShortBlocks = numBlocks - rawCodewords % numBlocks;
	int shortBlockLen  = rawCodewords / numBlocks;

	// Split data into blocks and append ECC to each block
	vector<vector<uint8_t> > blocks;
	const vector<uint8_t> rsDiv = reedSolomonComputeDivisor(blockEccLen);
	for (int i = 0, k = 0; i < numBlocks; i++) {
		vector<uint8_t> dat(data.cbegin() + k, data.cbegin() + (k + shortBlockLen - blockEccLen + (i < numShortBlocks ? 0 : 1)));
		k += static_cast<int>(dat.size());
		const vector<uint8_t> ecc = reedSolomonComputeRemainder(dat, rsDiv);
		if (i < numShortBlocks)
			dat.push_back(0);
		dat.insert(dat.end(), ecc.cbegin(), ecc.cend());
		blocks.push_back(std::move(dat));
	}

	// Interleave (not concatenate) the bytes from every block into a single sequence
	vector<uint8_t> result;
	for (size_t i = 0; i < blocks.at(0).size(); i++) {
		for (size_t j = 0; j < blocks.size(); j++) {
			// Skip the padding byte in short blocks
			if (i != static_cast<unsigned int>(shortBlockLen - blockEccLen) || j >= static_cast<unsigned int>(numShortBlocks))
				result.push_back(blocks.at(j).at(i));
		}
	}
	assert(result.size() == static_cast<unsigned int>(rawCodewords));
	return result;
}

void QrCode::drawCodewords(const vector<uint8_t> &data) {
	size_t i = 0;	// Bit index into the data
	// Do the funny zigzag scan
	for (int right = size - 1; right >= 1; right -= 2) {  // Index of right column in each column pair
		if (right == 6)
			right = 5;
		for (int vert = 0; vert < size; vert++) {  // Vertical counter
			for (int j = 0; j < 2; j++) {
				size_t x	  = static_cast<size_t>(right - j);  // Actual x coordinate
				bool upward = ((right + 1) & 2) == 0;
				size_t y	  = static_cast<size_t>(upward ? size - 1 - vert : vert);  // Actual y coordinate
				if (!isFunction.at(y).at(x) && i < data.size() * 8) {
					modules.at(y).at(x) = getBit(data.at(i >> 3), 7 - static_cast<int>(i & 7));
					i++;
				}
				// If this QR Code has any remainder bits (0 to 7), they were assigned as
				// 0/false/light by the constructor and are left unchanged by this method
			}
		}
	}
	assert(i == data.size() * 8);
}

void QrCode::applyMask(int msk) {
	// if (msk < 0 || msk > 7)
	//	throw std::domain_error("Mask value out of range");
	size_t sz = static_cast<size_t>(size);
	for (size_t y = 0; y < sz; y++) {
		for (size_t x = 0; x < sz; x++) {
			bool invert = false;
			switch (msk) {
				case 0:
					invert = (x + y) % 2 == 0;
					break;
				case 1:
					invert = y % 2 == 0;
					break;
				case 2:
					invert = x % 3 == 0;
					break;
				case 3:
					invert = (x + y) % 3 == 0;
					break;
				case 4:
					invert = (x / 3 + y / 2) % 2 == 0;
					break;
				case 5:
					invert = x * y % 2 + x * y % 3 == 0;
					break;
				case 6:
					invert = (x * y % 2 + x * y % 3) % 2 == 0;
					break;
				case 7:
					invert = ((x + y) % 2 + x * y % 3) % 2 == 0;
					break;
				// default:  throw std::logic_error("Unreachable");
				default:
					break;
			}
			modules.at(y).at(x) = modules.at(y).at(x) ^ (invert & !isFunction.at(y).at(x));
		}
	}
}

long QrCode::getPenaltyScore() const {
	long result = 0;

	// Adjacent modules in row having same color, and finder-like patterns
	for (int y = 0; y < size; y++) {
		bool runColor				= false;
		int runX					= 0;
		std::array<int, 7> runHistory = {};
		for (int x = 0; x < size; x++) {
			if (module(x, y) == runColor) {
				runX++;
				if (runX == 5)
					result += PENALTY_N1;
				else if (runX > 5)
					result++;
			} else {
				finderPenaltyAddHistory(runX, runHistory);
				if (!runColor)
					result += finderPenaltyCountPatterns(runHistory) * PENALTY_N3;
				runColor = module(x, y);
				runX	    = 1;
			}
		}
		result += finderPenaltyTerminateAndCount(runColor, runX, runHistory) * PENALTY_N3;
	}
	// Adjacent modules in column having same color, and finder-like patterns
	for (int x = 0; x < size; x++) {
		bool runColor				= false;
		int runY					= 0;
		std::array<int, 7> runHistory = {};
		for (int y = 0; y < size; y++) {
			if (module(x, y) == runColor) {
				runY++;
				if (runY == 5)
					result += PENALTY_N1;
				else if (runY > 5)
					result++;
			} else {
				finderPenaltyAddHistory(runY, runHistory);
				if (!runColor)
					result += finderPenaltyCountPatterns(runHistory) * PENALTY_N3;
				runColor = module(x, y);
				runY	    = 1;
			}
		}
		result += finderPenaltyTerminateAndCount(runColor, runY, runHistory) * PENALTY_N3;
	}

	// 2*2 blocks of modules having same color
	for (int y = 0; y < size - 1; y++) {
		for (int x = 0; x < size - 1; x++) {
			bool color = module(x, y);
			if (color == module(x + 1, y) &&
			    color == module(x, y + 1) &&
			    color == module(x + 1, y + 1))
				result += PENALTY_N2;
		}
	}

	// Balance of dark and light modules
	int dark = 0;
	for (const vector<bool> &row : modules) {
		for (bool color : row) {
			if (color)
				dark++;
		}
	}
	int total = size * size;	 // Note that size is odd, so dark/total != 1/2
	// Compute the smallest integer k >= 0 such that (45-5k)% <= dark/total <= (55+5k)%
	int k = static_cast<int>((std::abs(dark * 20L - total * 10L) + total - 1) / total) - 1;
	assert(0 <= k && k <= 9);
	result += k * PENALTY_N4;
	assert(0 <= result && result <= 2568888L);  // Non-tight upper bound based on default values of PENALTY_N1, ..., N4
	return result;
}

vector<int> QrCode::getAlignmentPatternPositions() const {
	if (version == 1)
		return vector<int>();
	else {
		int numAlign = version / 7 + 2;
		int step	   = (version == 32) ? 26 : (version * 4 + numAlign * 2 + 1) / (numAlign * 2 - 2) * 2;
		vector<int> result;
		for (int i = 0, pos = size - 7; i < numAlign - 1; i++, pos -= step)
			result.insert(result.begin(), pos);
		result.insert(result.begin(), 6);
		return result;
	}
}

int QrCode::getNumRawDataModules() {
	int result = (16 * version + 128) * version + 64;

	int numAlign = version / 7 + 2;
	result -= (25 * numAlign - 10) * numAlign - 55;

	assert(208 <= result && result <= 29648);
	return result;
}

int QrCode::getNumDataCodewords() {
	return getNumRawDataModules() / 8 - ECC_CODEWORDS_PER_BLOCK * NUM_ERROR_CORRECTION_BLOCKS;
}

vector<uint8_t> QrCode::reedSolomonComputeDivisor(int degree) {
	// if (degree < 1 || degree > 255)
	//	throw std::domain_error("Degree out of range");
	// Polynomial coefficients are stored from highest to lowest power, excluding the leading term which is always 1.
	// For example the polynomial x^3 + 255x^2 + 8x + 93 is stored as the uint8 array {255, 8, 93}.
	vector<uint8_t> result(static_cast<size_t>(degree));
	result.at(result.size() - 1) = 1;	// Start off with the monomial x^0

	// Compute the product polynomial (x - r^0) * (x - r^1) * (x - r^2) * ... * (x - r^{degree-1}),
	// and drop the highest monomial term which is always 1x^degree.
	// Note that r = 0x02, which is a generator element of this field GF(2^8/0x11D).
	uint8_t root = 1;
	for (int i = 0; i < degree; i++) {
		// Multiply the current product by (x - r^i)
		for (size_t j = 0; j < result.size(); j++) {
			result.at(j) = reedSolomonMultiply(result.at(j), root);
			if (j + 1 < result.size())
				result.at(j) ^= result.at(j + 1);
		}
		root = reedSolomonMultiply(root, 0x02);
	}
	return result;
}

vector<uint8_t> QrCode::reedSolomonComputeRemainder(const vector<uint8_t> &data, const vector<uint8_t> &divisor) {
	vector<uint8_t> result(divisor.size());
	for (uint8_t b : data) {	 // Polynomial division
		uint8_t factor = b ^ result.at(0);
		result.erase(result.begin());
		result.push_back(0);
		for (size_t i = 0; i < result.size(); i++)
			result.at(i) ^= reedSolomonMultiply(divisor.at(i), factor);
	}
	return result;
}

uint8_t QrCode::reedSolomonMultiply(uint8_t x, uint8_t y) {
	// Russian peasant multiplication
	int z = 0;
	for (int i = 7; i >= 0; i--) {
		z = (z << 1) ^ ((z >> 7) * 0x11D);
		z ^= ((y >> i) & 1) * x;
	}
	assert(z >> 8 == 0);
	return static_cast<uint8_t>(z);
}

int QrCode::finderPenaltyCountPatterns(const std::array<int, 7> &runHistory) const {
	int n = runHistory.at(1);
	assert(n <= size * 3);
	bool core = n > 0 && runHistory.at(2) == n && runHistory.at(3) == n * 3 && runHistory.at(4) == n && runHistory.at(5) == n;
	return (core && runHistory.at(0) >= n * 4 && runHistory.at(6) >= n ? 1 : 0) + (core && runHistory.at(6) >= n * 4 && runHistory.at(0) >= n ? 1 : 0);
}

int QrCode::finderPenaltyTerminateAndCount(bool currentRunColor, int currentRunLength, std::array<int, 7> &runHistory) const {
	if (currentRunColor) {  // Terminate dark run
		finderPenaltyAddHistory(currentRunLength, runHistory);
		currentRunLength = 0;
	}
	currentRunLength += size;  // Add light border to final run
	finderPenaltyAddHistory(currentRunLength, runHistory);
	return finderPenaltyCountPatterns(runHistory);
}

void QrCode::finderPenaltyAddHistory(int currentRunLength, std::array<int, 7> &runHistory) const {
	if (runHistory.at(0) == 0)
		currentRunLength += size;  // Add light border to initial run
	std::copy_backward(runHistory.cbegin(), runHistory.cend() - 1, runHistory.end());
	runHistory.at(0) = currentRunLength;
}

bool QrCode::getBit(long x, int i) {
	return ((x >> i) & 1) != 0;
}

/*---- Tables of constants ----*/

const int QrCode::PENALTY_N1 = 3;
const int QrCode::PENALTY_N2 = 3;
const int QrCode::PENALTY_N3 = 40;
const int QrCode::PENALTY_N4 = 10;

/*---- Class BitBuffer ----*/

BitBuffer::BitBuffer()
    : std::vector<bool>() {}

void BitBuffer::appendBits(std::uint32_t val, int len) {
	// if (len < 0 || len > 31 || val >> len != 0)
	//	throw std::domain_error("Value out of range");
	for (int i = len - 1; i >= 0; i--)	 // Append bit by bit
		this->push_back(((val >> i) & 1) != 0);
}

}  // namespace qrcodegen