#include <DGtal/base/Common.h>
#include <DGtal/helpers/StdDefs.h>
#include <DGtal/images/ImageSelector.h>
#include "DGtal/io/readers/PGMReader.h"
#include "DGtal/io/writers/GenericWriter.h"
#include <DGtal/images/imagesSetsUtils/SetFromImage.h>
#include <DGtal/io/boards/Board2D.h>
#include <DGtal/io/colormaps/ColorBrightnessColorMap.h>
#include "DGtal/geometry/curves/estimation/DSSLengthEstimator.h"
#include <DGtal/topology/SurfelAdjacency.h>
#include <DGtal/topology/helpers/Surfaces.h>
#include "DGtal/io/Color.h"
#include "DGtal/topology/KhalimskySpaceND.h"
#include "DGtal/topology/helpers/Surfaces.h"
#include "DGtal/base/Common.h"
#include "DGtal/helpers/StdDefs.h"
#include "DGtal/base/BasicFunctors.h"
#include "DGtal/kernel/BasicPointPredicates.h"
#include "DGtal/io/readers/PGMReader.h"
#include "DGtal/images/ImageContainerBySTLVector.h"
#include <cmath>
#include <sys/stat.h>
#include <sys/types.h>
#include <iostream>
#include <fstream>


using namespace std;
using namespace DGtal;
using namespace Z2i;



template<class T>
void sendToBoard( Board2D & board, T & p_Object, Color p_Color) {
    board << CustomStyle( p_Object.className(), new CustomFillColor(p_Color));
    board << p_Object;
}

template<class Image>
Image importImageWithBorder(const string& name) {
  Image image = PGMReader<Image>::importPGM (name);

  // Get image size
  const Point& upper = image.domain().upperBound();
  const int width = upper[0] + 1;
  const int height = upper[1] + 1;
  // Add border
  for (int i = 0; i < width; i++) {
    image.setValue(Point(i, 0), 255);
    image.setValue(Point(i, height-1), 255);
  }
  for (int i = 0; i < height; i++) {
    image.setValue(Point(0, i), 255);
    image.setValue(Point(width-1, i), 255);
  }
  return image;
}

template<class ObjectType>
void remove_border_component(vector<ObjectType>& components) {
  // Remove the first and only component that have a x-coordinate at 0
  for (auto it = components.begin(); it != components.end(); it++) {
    for (const auto& point : *it) {
      if (point[0] == 0) {
        components.erase(it);
        return;
      }
    }
  }
}

template<class ObjectType, class Image, class DigitalTopology>
vector< ObjectType > find_components(DigitalSet& point_set, const Image& image, const DigitalTopology& dt) {
  // create 2d set of all points
  SetFromImage< DigitalSet >::append< Image >(point_set, image, 1, 255); // Add pixel not black

  // Create object storage
  vector< ObjectType > components;
  back_insert_iterator< vector< ObjectType > > inserter(components);
  ObjectType(dt, point_set).writeComponents(inserter);
  return components;
}

template<class ObjectType>
vector<SCell> get_boundary(const Domain& domain, const DigitalSet& point_set, const ObjectType& object) {
  typedef KhalimskySpaceND< 2, int > KSpace;

  KSpace ks;
  bool space_ok = ks.init( domain.lowerBound(), domain.upperBound(), true );
  auto aCell = Surfaces<KSpace>::findABel(ks, object.pointSet(), 10000);

  vector<SCell> boundary;
  SurfelAdjacency<2> SAdj( true );
  Surfaces<KSpace>::track2DBoundary(boundary, ks, SAdj, point_set, aCell);

  return boundary;
}

template<class Decomposition>
void compute_area_and_perimeter(const Decomposition& decomposition, double& polygon_area, double& polygon_perimeter) {
  long sum = 0;
  for (auto it = decomposition.begin(); it != decomposition.end(); it.next()) {
    auto edge = *it;
    auto p1 = edge.back();
    auto p2 = edge.front();
    sum += p1[0] * p2[1] - p2[0] * p1[1];
    polygon_perimeter += sqrt(pow(p1[0] - p2[0], 2) + pow(p1[1] - p2[1], 2));
  }
  polygon_area = sum / 2.d;
}

template<class D>
void draw_polygon(Board2D& board, const D& decomposition) {
  for (auto it = decomposition.begin(); it != decomposition.end(); it.next()) {
    auto edge = *it;
    auto p1 = edge.back();
    auto p2 = edge.front();
    board.drawLine(p2[0], p2[1], p1[0], p1[1]);
  }
}

template<class ObjectType, class DigitalTopology>
unsigned int count_grain(string image_path, DigitalTopology dt, string dt_name) {
  typedef ImageSelector<Domain, unsigned char >::Type Image;
  typedef DigitalSetSelector< Domain, BIG_DS+HIGH_BEL_DS >::Type DigitalSet;
  typedef Curve::PointsRange Range; 
  typedef ArithmeticalDSSComputer<Curve::PointsRange::ConstIterator,int,4> DSS4;
  typedef GreedySegmentation<DSS4> Decomposition;

  // Create directory to store polygons
  string image_name = image_path.substr(image_path.rfind("/") + 1);
  string output_directory = "./" + image_name + "/" + dt_name;
  mkdir(image_name.data(), 0777);
  mkdir(output_directory.data(), 0777);
  // Open file
  ofstream file;
  file.open((output_directory + "/results.csv").data());
  file << "polygon, area (pxl), area (polygon), perimeter (pxl), perimeter (polygon), circularity" << endl;

  // Create a board on wich the image will be displayed
  // Load image
  Image image = importImageWithBorder<Image>(image_path);
  // Create components
  Domain domain = image.domain();
  DigitalSet point_set(domain);
  vector< ObjectType > components = find_components<ObjectType, Image, DigitalTopology>(point_set, image, dt);

  remove_border_component(components);

  Board2D board;
  for (int i = 0; i < components.size(); i++) {
    ObjectType object = components[i];  
    vector<SCell> boundary = get_boundary<ObjectType>(domain, point_set, object);

    Curve curve;
    curve.initFromSCellsVector( boundary ); 
    curve.push_back(*curve.begin());

    Range range = curve.getPointsRange();
    
    Decomposition decomposition(range.begin(),range.end(), DSS4());
    
    double polygon_area, polygon_perimeter;
    compute_area_and_perimeter(decomposition, polygon_area, polygon_perimeter);
    int pixel_perimeter = object.border().size();
    int pixel_area = object.size();
    double circularity = (4 * M_PI * pixel_area) / pow(pixel_perimeter, 2);

    file << i << "," << pixel_area << ", "<< polygon_area << ", " << pixel_perimeter 
          << ", " << polygon_perimeter << ", " << circularity << endl;
    draw_polygon(board, decomposition);
    board.saveSVG((output_directory + "/img_" + to_string(i) + ".svg").data());
    board.clear();
  }
  file.close();
  return components.size();
}


int main(int argc, char** argv)
{
    setlocale(LC_NUMERIC, "us_US"); //To prevent French local settings

    for (size_t i = 1; i < argc; i++) {
      auto name = argv[i];
      cout << name << "4_8 : " << count_grain<Object4_8, DT4_8>(name, dt4_8, "4_8") << endl;
      cout << name << "8_4 : " << count_grain<Object8_4, DT8_4>(name, dt8_4, "8_4") << endl;
    }

    return 0;
}
