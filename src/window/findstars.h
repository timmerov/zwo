/*
Copyright (C) 2012-2025 tim cotter. All rights reserved.
*/

/**
data structures for finding stars in the images.
and manipulating lists of stars.

window thread.
**/


namespace WindowThread {

class StarPosition {
public:
    /** position. **/
    double x_ = 0.0;
    double y_ = 0.0;
    /** radius of drawn circle. **/
    int r_;
    /** sums for updating centroid. **/
    double sum_x_ = 0.0;
    double sum_y_ = 0.0;
    double sum_ = 0.0;
    /** max pixel value. **/
    int brightness_ = 0;
    /** bounding box for collisions. **/
    int left_ = 0;
    int top_ = 0;
    int right_ = 0;
    int bottom_ = 0;
    /** reliability. **/
    int found_ = 0;
    int missed_ = 0;
};
typedef std::vector<StarPosition> StarPositions;
typedef std::vector<StarPositions> StarLists;

class StarData {
public:
    /** positions of stars in current image. **/
    StarPositions positions_;

    /** state information. **/
    bool building_list_ = false;
    StarLists lists_;
};

} // WindowThread
