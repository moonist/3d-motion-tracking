#include "MotionTracker.h"

MotionTracker::MotionTracker(bool debug)
{
  this->debug = debug;
}

function<void (Mat)> MotionTracker::track()
{
  auto pipe = [&](Mat im)
  {
    trackMotion(im);
  };

  return pipe;
}

void MotionTracker::trackMotion(Mat &im)
{
  // Convert image to HSV
  Mat hsv;
  cvtColor(im, hsv, CV_BGR2HSV);

  // Blur the image for noise reduction
  Mat bhsv;
  GaussianBlur( hsv, bhsv, Size(3,3), 2.5, 2.5, BORDER_REFLECT_101);

  // Take only V component
  Mat arr[3];
  split(bhsv, arr);
  Mat h = arr[0];
  Mat s = arr[0];
  Mat v = arr[2];

  normalize(h, h, 0, 255, NORM_MINMAX, -1, Mat());
  medianBlur(h, h, 9);

  // Detect corners
  int wndSize    = 5;
  int maxCorners = 16;
  double minDist = 5;
  vector<Point2f> cornersS = TrackUtils::detectFeaturePoints(h, wndSize, maxCorners, minDist*3, 0.2);
  vector<Point2f> cornersV = TrackUtils::detectFeaturePoints(v, wndSize, maxCorners, minDist, 0.05);

  Mat canvas = im.clone();

  // Create a trackable mesh
  vector<Point2f> points = cornersS;
  points.insert(points.end(), cornersV.begin(), cornersV.end());
  if (this->debug)
    cout << "... " << points.size() << " vertices captured" << endl;
  MeshObject mesh(points);

  double maxEdgeLength   = im.size[0];
  double maxDisplacement = im.size[0]*0.833;

  // Split meshes
  vector<MeshObject> meshes = mesh.split(maxEdgeLength);
  if (this->debug)
    cout << "... " << meshes.size() << " meshes splitted" << endl;
  for (MeshObject m : meshes) 
    m.drawMesh(canvas, Scalar(100,100,200), Scalar(0,0,240), maxEdgeLength);

  // Align recently tracked meshes
  // with the newly tracked ones
  alignMeshes(meshes, maxDisplacement);

  namedWindow("tracked", CV_WINDOW_AUTOSIZE);
  imshow("tracked", canvas);
}

void MotionTracker::alignMeshes(vector<MeshObject> newMeshes, double maxDist)
{
  if (this->debug)
    cout << "...Aligning mesh: " << this->currMeshes.size() 
      << " --> " << newMeshes.size() 
      << endl;

  // If there was no previously tracked meshes,
  // just clone them
  if (this->currMeshes.size()==0)
  {
    copy(newMeshes.begin(), newMeshes.end(), back_inserter(this->currMeshes));
    return;
  }

  int N0 = this->currMeshes.size();
  int N1 = newMeshes.size();
  int N  = max(N0,N1);

  // Take only centroids into account
  vector<Point2f> centroids0;
  vector<Point2f> centroids;

  // Fill lists of centroids
  for (auto m : this->currMeshes)
    centroids0.push_back(m.centroid());
  for (auto m : newMeshes)
    centroids.push_back(m.centroid());

  // Map 1-to-N distrances
  // and create an assignment problem matrix M
  //     [row] => represents old mesh
  //     [col] => represents new mesh
  Mat m = Mat(N, N, CV_32F, Scalar(numeric_limits<float>::max()));
  int j = 0;
  for (auto c0 : centroids0)
  {
    int i = 0;
    for (auto c1 : centroids)
    {
      float d = (float)_dist(c0,c1);
      m.at<float>(j,i) = d;
      i++;
    }
    j++;
  }

  if (debug)
  {
    cout << "[M] " << N << " x " << N << endl;
  }

  // Use Hungarian algorithm to find best matches
  // of [currentMesh] and [prevMesh]
  Hungarian h(m, false); // Debug OFF
  vector<tuple<int,int>> match = h.optimiseMinima();

  // Couple the [prev] and [curr] meshes
  int nFreshNew = 0;
  int nAbsentOld = 0;
  int nUpdated = 0;
  queue<int> pendingForAdd;
  for (auto couple : match)
  {
    int i0 = get<0>(couple); // [Row] = old mesh ID
    int i1 = get<1>(couple); // [Col] = new mesh ID

    // Ignore if this matches nothing
    if (i0>=N0 && i1>=N1)
      continue;
    // [new] with no matching
    else if (i0>=N0)
    {
      nFreshNew ++;
      pendingForAdd.push(i1);
    }
    // [old] with no matching
    else if (i1>=N1)
    {
      nAbsentOld++;
      this->currMeshes[i0].lengthOfAbsence++;
    }
    // Perfect match between [old] and [new]
    else
    {
      // Record the movement history
      nUpdated++;
      this->currMeshes[i0].update(newMeshes[i1]);
    }
  }

  if (debug)
  {
    cout << "... " << nUpdated << " mesh(es) updated" << endl;
    cout << "... " << nAbsentOld << " mesh(es) absent" << endl;
    cout << "... " << nFreshNew << " new mesh(es)" << endl;
  }

  // Add the new meshes
  while (!pendingForAdd.empty())
  {
    int n = pendingForAdd.front();
    pendingForAdd.pop();
    this->currMeshes.push_back(newMeshes[n]);
  }

  // TAOTODO: Remove meshes which have been absent too long
  

}

Mat MotionTracker::calcHistBackProjection(Mat& im)
{
  Mat hist, bproj;
  Mat mask = Mat();
  int nChannels[] = {0};
  int histBins[]  = {16};
  float rangeValues[] = {0, 180};
  const float* ranges[] = {rangeValues};
  calcHist(&im, 1, nChannels, mask, hist, 1, histBins, ranges, true, false);
  normalize(hist, hist, 0, 255, NORM_MINMAX, -1, mask);
  GaussianBlur( hist, hist, Size(3,3), 10.0, 10.0, BORDER_REFLECT_101);
  calcBackProject(&im, 1, nChannels, hist, bproj, ranges);
  return bproj;
}
