#include <math.h>
#include <iostream>
#include <chrono>


#include "FaceSMProcess.h"

inline SDFGenerator::Point SDFGenerator::Get(Grid& g, int x, int y) {
    return g.points[(y + 1) * gridWidth + (x + 1)];
}

inline void SDFGenerator::Put(Grid& g, int x, int y, const Point& p) {
    g.points[(y + 1) * gridWidth + (x + 1)] = p;
}


inline SDFGenerator::Point SDFGenerator::GroupCompare(Grid& g, Point other,
    int x, int y, const __m256i& offsets) {
    Point self = Get(g, x, y);

    /* Point other = Get( g, x+offsetx, y+offsety ); */
    int* offsetsPtr = (int*)&offsets;
    Point pn[4] = {
        other,
        Get(g, x + offsetsPtr[1], y + offsetsPtr[5]),
        Get(g, x + offsetsPtr[2], y + offsetsPtr[6]),
        Get(g, x + offsetsPtr[3], y + offsetsPtr[7]),
    };

    /* other.dx += offsetx; other.dy += offsety; */
    __m256i* pnPtr = (__m256i*)pn;
    // x0, y0, x1, y1, x2, y2, x3, y3 -> x0, x1, x2, x3, y0, y1, y2, y3
    static const __m256i mask = _mm256_setr_epi32(0, 2, 4, 6, 1, 3, 5, 7);
    __m256i vecCoords = _mm256_permutevar8x32_epi32(*pnPtr, mask);
    vecCoords = _mm256_add_epi32(vecCoords, offsets);

    /* other.DistSq() */
    int* coordsPtr = (int*)&vecCoords;
    // note that _mm256_mul_epi32 only applies on the lower 128 bits
    __m256i vecPermuted = _mm256_permute2x128_si256(vecCoords, vecCoords, 1);
    __m256i vecSqrDists = _mm256_add_epi64(_mm256_mul_epi32(vecCoords, vecCoords),
        _mm256_mul_epi32(vecPermuted, vecPermuted));

    /* if (other.DistSq() < p.DistSq()) p = other; */
    int64_t prevDist = self.distSq(), index = -1;
    for (int i = 0; i < 4; ++i) {
        int64_t dist = *((int64_t*)&vecSqrDists + i);
        if (dist < prevDist) {
            prevDist = dist;
            index = i;
        }
    }
    if (index != -1) {
        other = { coordsPtr[index], coordsPtr[index + 4] };
        Put(g, x, y, other);
        return other;
    }
    else {
        return self;
    }
}

inline SDFGenerator::Point SDFGenerator::SingleCompare(Grid& g, Point other,
    int x, int y, int offsetx, int offsety) {
    Point self = Get(g, x, y);
    other.dx += offsetx;
    other.dy += offsety;

    if (other.distSq() < self.distSq()) {
        Put(g, x, y, other);
        return other;
    }
    else {
        return self;
    }
}

float SDFGenerator::GenerateSDF(Grid& g) {
    // Pass 0
    double maxValue = -1;

    static const __m256i offsets0 = _mm256_setr_epi32(-1, -1, 0, 1, 0, -1, -1, -1);
    for (int y = 0; y < imageHeight; ++y) {
        Point prev = Get(g, -1, y);
        for (int x = 0; x < imageWidth; ++x)
            prev = GroupCompare(g, prev, x, y, offsets0);

        prev = Get(g, imageWidth, y);
        for (int x = imageWidth - 1; x >= 0; --x)
            prev = SingleCompare(g, prev, x, y, 1, 0);
    }

    // Pass 1
    /*
    static const __m256i offsets1 = _mm256_setr_epi32(1, -1, 0, 1, 0, 1, 1, 1);
    for (int y = imageHeight - 1; y >= 0; --y) {
        Point prev = get(g, imageWidth, y);
        for (int x = imageWidth - 1; x >= 0; --x)
            prev = groupCompare(g, prev, x, y, offsets1);


        prev = get(g, -1, y);
        for (int x = 0; x < imageWidth; ++x){
            prev = singleCompare(g, prev, x, y, -1, 0);
            if (maxValue < prev.distSq())
            {
                maxValue = prev.distSq();
            }
        }
    }
    */
    // Pass 1
    for (int y = imageHeight - 1; y >= 0; y--)
    {
        for (int x = imageWidth - 1; x >= 0; x--)
        {
            Point p = Get(g, x, y);
            Compare(g, p, x, y, 1, 0);
            Compare(g, p, x, y, 0, 1);
            Compare(g, p, x, y, -1, 1);
            Compare(g, p, x, y, 1, 1);
            Put(g, x, y, p);
        }

        for (int x = 0; x < imageWidth; x++)
        {
            Point p = Get(g, x, y);
            Compare(g, p, x, y, -1, 0);
            Put(g, x, y, p);
            if (maxValue < p.distSq())
            {
                maxValue = p.distSq();
            }
        }
    }
    return (float)sqrt(maxValue);
}


struct PackData
{
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
};

void SDFGenerator::Load(int width, int height, unsigned char* image) {
    imageWidth = width;
    imageHeight = height;
    gridWidth = imageWidth + 2;
    gridHeight = imageHeight + 2;
    numPoint = gridWidth * gridHeight; // include padding

    if (grid1.points) free(grid1.points);
    if (grid2.points) free(grid2.points);

    grid1.points = (Point*)malloc(numPoint * sizeof(Point));
    grid2.points = (Point*)malloc(numPoint * sizeof(Point));

    PackData* Pack = (PackData*)image;
    for (int y = 0; y < imageHeight; ++y) {
        for (int x = 0; x < imageWidth; ++x) {
            if (Pack[y * imageWidth + x].r < 128) {
                Put(grid1, x, y, inside);
                Put(grid2, x, y, empty);
            }
            else {
                Put(grid2, x, y, inside);
                Put(grid1, x, y, empty);
            }
        }
    }
    for (int x = 0; x < imageWidth; ++x) { // top and buttom padding
        Put(grid2, x, -1, Get(grid2, x, 0));
        Put(grid1, x, -1, Get(grid1, x, 0));
        Put(grid2, x, imageHeight, Get(grid2, x, imageHeight - 1));
        Put(grid1, x, imageHeight, Get(grid1, x, imageHeight - 1));
    }
    for (int y = -1; y <= imageHeight; ++y) { // left and right padding
        Put(grid2, -1, y, Get(grid2, 0, y));
        Put(grid1, -1, y, Get(grid1, 0, y));
        Put(grid2, imageWidth, y, Get(grid2, imageWidth - 1, y));
        Put(grid1, imageWidth, y, Get(grid1, imageWidth - 1, y));
    }
}

void SDFGenerator::Run(int width, int height, unsigned char* image, unsigned char** output) {

    Load(width, height, image);

    Grid testGrid1, testGrid2;
    testGrid1.points = (Point*)malloc(numPoint * sizeof(Point));
    testGrid2.points = (Point*)malloc(numPoint * sizeof(Point));
 
    float insideMax = 0;
    float outsideMax = 0;

    memcpy(testGrid1.points, grid1.points, numPoint * sizeof(Point));
    memcpy(testGrid2.points, grid2.points, numPoint * sizeof(Point));

    insideMax = GenerateSDF(testGrid1);
    outsideMax = GenerateSDF(testGrid2);


    std::cout << "insideMax " << insideMax << " | outsideMax " << outsideMax << std::endl;
    PackData* Pack = (PackData*)(*output);
    for (int y = 0; y < imageHeight; ++y) {
        for (int x = 0; x < imageWidth; ++x) {
            // calculate the actual distance from the dx/dy
            int dist1 = (int)(sqrt((double)Get(testGrid1, x, y).distSq()));
            int dist2 = (int)(sqrt((double)Get(testGrid2, x, y).distSq()));
            int dist = dist1 - dist2;

            // scale for display purpose
            float c = 0.5;
            if (dist < 0)
            {
                c += dist / outsideMax * 0.5f;
            }
            else
            {
                c += dist / insideMax * 0.5f;
            }

            int output_color = int(c * 255);
            (Pack)[y * imageWidth + x].r = output_color;
            (Pack)[y * imageWidth + x].g = output_color;
            (Pack)[y * imageWidth + x].b = output_color;
            (Pack)[y * imageWidth + x].a = output_color;
        }
    }

    //stbi_write_jpg("Test5.jpg", imageWidth, imageHeight, 4, *output, 100);

    if (testGrid1.points) free(testGrid1.points);
    if (testGrid2.points) free(testGrid2.points);

}

