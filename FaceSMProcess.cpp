#include <math.h>
#include <iostream>
#include <chrono>

#include "FaceSMProcess.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image\stb-master\stb_image_write.h"

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


    std::cout << "Side1£º " << insideMax << " | Side2£º " << outsideMax << std::endl;
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


double Lerp(double a, double b, double t)
{
    /*
    float v = t > 1.0f ? 1.0f : t;
    v = v < 0.0f ? 0.0f : v;
    */
    return a + t * (b - a);
}


void ImageBaker::Prepare(int Height, int Width, std::vector<unsigned char*>& Sources)
{
    BakeCompleted = true;
    BlurCompleted = true;
    ImageWrote = true;
    if (Sources.size() < 2) return;

    Cleanup();

    for (unsigned char* data : Sources)
    {
        SourceList.push_back((PackData*)data);
    }
    
    ImageHeight = Height;
    ImageWidth = Width;
    ImageSize = Height * Width;
    BakedImage = (PackData*)malloc(ImageSize * sizeof(PackData));
    memset(BakedImage, 0, ImageSize * sizeof(PackData));
   
    //ProgressPerSampleTimes = (1 / ((double)ImageSize * (double)SampleTimes * (SourceList.size() - 1)));
    ProgressPerBakeStep = (1 / ((double)ImageSize * (SourceList.size() - 1)));
    ProgressPerBlurStep = 1 / (double)ImageSize;

    std::cout << "Prepare to bake:" << std::endl;
    std::cout << "FileName : " << OutputFileName << std::endl;
    std::cout << "SampleTimes : " << SampleTimes << std::endl;
    std::cout << "ProgressPerBakeStep : " << ProgressPerBakeStep << std::endl;
    std::cout << "ProgressPerBlurStep : " << ProgressPerBlurStep << std::endl;
    //std::cout << "ProgressPerSampleTimes : " << ProgressPerSampleTimes << std::endl;
    
    BakeCompleted = false;
    BlurCompleted = false;
    ImageWrote = false;
}


double ImageBaker::RunBakeStep()
{
    if (BakeCompleted) {
        return 0.0;
    }

    PackData* SourceTexture0 = SourceList[CurrentSourcePos];
    PackData* SourceTexture1 = SourceList[CurrentSourcePos+1];

    bool Skip = false;
    double Progress = 0.0;
    
    double s0 = SourceTexture0[CurrentPixelPos].r;
    double s1 = SourceTexture1[CurrentPixelPos].r;

    // Directly return if this two situation is true, we do not need to loop because the result is same as the loop below 
    if (s0 < 125 && s1 < 125)
    {
        BakedImage[CurrentPixelPos].r += 0;
        BakedImage[CurrentPixelPos].g += 0;
        BakedImage[CurrentPixelPos].b += 0;
        BakedImage[CurrentPixelPos].a = 255;
        CurrentPixelPos += 1;
        CurrentColorValue = 0.0;
        CurrentSampleTimes = 0;
        Skip = true;
        Progress = ProgressPerBakeStep;
    }
    else if (s0 > 132 && s1 > 132)
    {
        unsigned char Color = unsigned char(CalculateFinalColor(1) * 255.0);
        BakedImage[CurrentPixelPos].r += Color;
        BakedImage[CurrentPixelPos].g += Color;
        BakedImage[CurrentPixelPos].b += Color;
        BakedImage[CurrentPixelPos].a = 255;
        CurrentPixelPos += 1;
        CurrentColorValue = 0.0;
        CurrentSampleTimes = 0;
        Skip = true;
        Progress = ProgressPerBakeStep;
    }
    
    if (!Skip) {

        double Color0 = s0 / 255.0;
        double Color1 = s1 / 255.0;

        for (int i = 0; i < SAMPLE_STEP; i++)
        {
            double Value = CurrentSampleTimes / SampleTimes;
            CurrentColorValue += Lerp(Color0, Color1, Value) < 0.49999 ? 0.0 : 1.0;
            CurrentSampleTimes++;
            //Progress += ProgressPerSampleTimes;
            if (CurrentSampleTimes >= SampleTimes)
                break;
        }

        // A sample loop is finished, should move to next pixel
        if (CurrentSampleTimes >= SampleTimes) {
            CurrentColorValue = CurrentColorValue / (double)SampleTimes;
            unsigned char OutColor =  unsigned char(CalculateFinalColor(CurrentColorValue) * 255.0);
            BakedImage[CurrentPixelPos].r += OutColor;
            BakedImage[CurrentPixelPos].g += OutColor;
            BakedImage[CurrentPixelPos].b += OutColor;
            BakedImage[CurrentPixelPos].a = 255;

            CurrentPixelPos += 1;
            CurrentColorValue = 0.0;
            CurrentSampleTimes = 0;
            Progress = ProgressPerBakeStep;
            //std::cout << "Switch to next pixel: " << CurrentPixelPos << std::endl;
        }
    }

    // All pxiel of two sample texture is done, should move to next texture
    if (CurrentPixelPos >= ImageSize)
    {
        std::cout << "Switch to next 2 texture: " << CurrentSourcePos  << " | " << CurrentSourcePos + 1 << std::endl;
        std::cout << "Current pixel: " << CurrentPixelPos << std::endl;

        CurrentSourcePos += 1;
        CurrentPixelPos = 0;
        if ((CurrentSourcePos+1) >= (int)SourceList.size()) {
            //Done, wait for blur and output
            BakeCompleted = true;
            BlurCompleted = false;
            ImageWrote = false;

            if (BlurredImage == nullptr)
            {
                BlurredImage = (PackData*)malloc(ImageSize * sizeof(PackData));
            }
        }
    }

    return Progress;
}


#define ROW_WEIGHT 0.15
#define COL_WEIGHT 0.15

double ImageBaker::RunBlurStep()
{
    if (BlurCompleted) {
        return 0.0;
    }


    BlurredImage[CurrentPixelPos].r = BakedImage[CurrentPixelPos].r;
    BlurredImage[CurrentPixelPos].g = BakedImage[CurrentPixelPos].g;
    BlurredImage[CurrentPixelPos].b = BakedImage[CurrentPixelPos].b;
    BlurredImage[CurrentPixelPos].a = 255;
    CurrentPixelPos += 1;
   
    if (CurrentPixelPos >= CurrentBlurRow * ImageWidth)
        CurrentBlurRow += 1;

    if (CurrentPixelPos >= ImageSize)
    {
        BlurCompleted = true;
    }

    return ProgressPerBlurStep;
    /*
    if (BakedImage[CurrentPixelPos].r == 0 || BakedImage[CurrentPixelPos].r == 255)
    {
        BlurredImage[CurrentPixelPos].r = BakedImage[CurrentPixelPos].r;
        BlurredImage[CurrentPixelPos].g = BakedImage[CurrentPixelPos].g;
        BlurredImage[CurrentPixelPos].b = BakedImage[CurrentPixelPos].b;
        BlurredImage[CurrentPixelPos].a = 255;
        CurrentPixelPos += 1;
        return ProgressPerBlurStep;
    }
    double CurrentPixel = BakedImage[CurrentPixelPos].r / 255;

    int OffsetL, OffsetR, OffsetT, OffsetB;
    int OffsetLT, OffsetRT, OffsetLB, OffsetRB;

    int CurrentRowPos = CurrentBlurRow * ImageWidth;

    double Sample = CurrentPixel * (1.0 - 2 * ROW_WEIGHT - 2 * COL_WEIGHT);
    double RowWeight = (ROW_WEIGHT / BlurSize);
    double ColWeight = (COL_WEIGHT / BlurSize);
    for (int i = 1; i <= BlurSize; i++)
    {
        // left
        OffsetL = CurrentPixelPos - i;
        if (OffsetL >= CurrentRowPos) {
            Sample += (BakedImage[OffsetL].r / 255) * RowWeight;
        }
        // right 
        OffsetR = CurrentPixelPos + i;
        if (OffsetR < (CurrentRowPos+ImageWidth)) {
            Sample += (BakedImage[OffsetR].r / 255) * RowWeight;
        }
        // top
        OffsetT = CurrentPixelPos - i*ImageWidth;
        if (OffsetT >= 0) {
            Sample += (BakedImage[OffsetT].r / 255) * ColWeight;
        }
        // bottom
        OffsetB = CurrentPixelPos + i * ImageWidth;
        if (OffsetB < ImageSize) {
            Sample += (BakedImage[OffsetB].r / 255) * ColWeight;
        }
    }

    unsigned char Color = unsigned char(Sample * 255.0);
    BlurredImage[CurrentPixelPos].r = Color;
    BlurredImage[CurrentPixelPos].g = Color;
    BlurredImage[CurrentPixelPos].b = Color;
    BlurredImage[CurrentPixelPos].a = 255;

   
    CurrentPixelPos += 1;
    */
    
    return ProgressPerBlurStep;
}


double ImageBaker::RunWriteStep()
{
    if (ImageWrote) {
        return 0.0;
    }

    if (stbi_write_png(OutputFileName.c_str(), ImageWidth, ImageHeight, 4, (unsigned char*)BlurredImage, ImageWidth * 4) == 0)
        std::cerr << "Write image failed: "<< OutputFileName.c_str() << std::endl;
    //stbi_write_jpg("Test5.jpg", ImageWidth, ImageHeight, 4, (unsigned char*)OutputImage, 100);

    std::cout << "Output image finished :" << OutputFileName.c_str() << std::endl;
    ImageWrote = true;
    //Cleanup();

    return 0.0;
}



double ImageBaker::CalculateFinalColor(double Color)
{
    int NumLayers = (int)SourceList.size()-1;

    if (NumLayers >= 2) {
        /*
        double Range = 1.0 / double(NumLayers) + 0.000001;

        double StartRange = CurrentSourcePos * Range;
        double EndRange = StartRange + Range;
        EndRange = EndRange > 1.0 ? 1.0 : EndRange;

        return (Color - StartRange) / (EndRange - StartRange);
        */
        return Color / double(NumLayers) + 0.001;
    }
    else
    {
        return Color;
    }
}
