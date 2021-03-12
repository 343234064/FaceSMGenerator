#pragma once

#include <immintrin.h>

////////////////////////////////////////////////
//8ssedt optimized
//https://www.jianshu.com/p/58271568781d
///////////////////////////////////////////////

class SDFGenerator {
public:
    struct Point {
        int dx, dy;
        int distSq() const { return dx * dx + dy * dy; }
    };
    struct Grid {
        Point* points;
    };

public:
    ~SDFGenerator() {
        if (grid1.points) free(grid1.points);
        if (grid2.points) free(grid2.points);
    }

    
    void Run(int width, int height, unsigned char* image, unsigned char** output);

private:
    void Load(int width, int height, unsigned char* image);
    Point Get(Grid& g, int x, int y);
    void Put(Grid& g, int x, int y, const Point& p);
    Point GroupCompare(Grid& g, Point other, int x, int y, const __m256i& offsets);
    Point SingleCompare(Grid& g, Point other, int x, int y, int offsetx, int offsety);
    float GenerateSDF(Grid& g);

    void Compare(Grid& g, Point& p, int x, int y, int offsetx, int offsety)
    {
        Point other = Get(g, x + offsetx, y + offsety);
        other.dx += offsetx;
        other.dy += offsety;

        if (other.distSq() < p.distSq())
            p = other;
    }

private:

    int imageWidth, imageHeight;
    int gridWidth, gridHeight, numPoint;
    Grid grid1, grid2;
    Point inside = { 0, 0 };
    Point empty = { 16384, 16384 };


};
