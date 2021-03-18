#pragma once

#include <immintrin.h>
#include <vector>

struct BakeSettting
{
public:
    BakeSettting(int SampleNum = 0, int Height = 0, int Width = 0) :
        SampleTimes(SampleNum),
        Height(0),
        Width(0)
    {}
    std::string FileName;
    int SampleTimes;
    int Height;
    int Width;
};

struct PackData
{
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
};


struct TextureData
{
public:
    TextureData(int index = -1, int height = 0, int width = 0, unsigned char* data = nullptr):
        Index(index),
        Height(height),
        Width(width),
        Data(data),
        SDFData(nullptr)
    {}

    int Index;
    int Height;
    int Width;
    unsigned char* Data; // 4 channel
    unsigned char* SDFData; // 4 channel

    TextureData& operator=(TextureData& Other)
    {
        if (&Other == this) return *this;

        Index = Other.Index;
        Height = Other.Height;
        Width = Other.Width;
        Data = Other.Data;
        SDFData = Other.SDFData; 

        return *this;
    }
};

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
    SDFGenerator():
        imageWidth(0),
        imageHeight(0),
        gridWidth(0),
        gridHeight(0),
        numPoint(0)
    {
        inside = { 0, 0 };
        empty = { 16384, 16384 };
    }

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
    Point inside;
    Point empty;


};


class ImageBaker
{
#define SAMPLE_STEP 200

public:
    ImageBaker ():
        OutputImage(nullptr),
        OutputFileName("face_map_output.png"),
        SampleTimes(0),
        ImageHeight(0),
        ImageWidth(0),
        ImageSize(0),
        ProgressPerStep(0),
        //ProgressPerSampleTimes(0),
        Completed(true),
        ImageWrote(true),
        CurrentSourcePos(0),
        CurrentPixelPos(0),
        CurrentSampleTimes(0),
        CurrentColorValue(0)
    {}
    ~ImageBaker()
    {
        Cleanup();
    }

    void SetSampleTimes(int SampleNum) { SampleTimes = SampleNum; }
    void SetOutputFileName(std::string& FileName) { OutputFileName = FileName; }
    bool IsCompleted() { return Completed && ImageWrote; }

    void Prepare(int Height, int Width, std::vector<unsigned char*>& Sources);
    double RunStep();
    
    void Cleanup()
    {
        if (OutputImage != nullptr) {
            free(OutputImage);
            OutputImage = nullptr;
        }
        SourceList.clear();
        CurrentSourcePos = 0;
        CurrentPixelPos = 0;
        CurrentSampleTimes = 0;
        CurrentColorValue = 0;

        ProgressPerStep = 0;
        //ProgressPerSampleTimes = 0;

        ImageHeight = 0;
        ImageWidth = 0;
        ImageSize = 0;
    }

    PackData* GetOutputImage() { return OutputImage; }

private:
    double WriteImage();
    double CalculateFinalColor(double Color);

private:
    PackData* OutputImage;
    std::vector<PackData*> SourceList;
    
    std::string OutputFileName;
    int SampleTimes;

    int ImageHeight;
    int ImageWidth;
    int ImageSize;

    double ProgressPerStep;
    //double ProgressPerSampleTimes;

    /*Running states*/
    bool Completed;
    bool ImageWrote;

    int CurrentSourcePos;
    int CurrentPixelPos;
    double CurrentSampleTimes;
    double CurrentColorValue;

};