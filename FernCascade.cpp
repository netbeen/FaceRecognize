/*
 Author: Bi Sai
 Date: 2014/06/18
 This program is a reimplementation of algorithms in "Face Alignment by Explicit
 Shape Regression" by Cao et al.
 If you find any bugs, please email me: soundsilencebisai-at-gmail-dot-com

 Copyright (c) 2014 Bi Sai
 The MIT License (MIT)
 Permission is hereby granted, free of charge, to any person obtaining a copy of
 this software and associated documentation files (the "Software"), to deal in
 the Software without restriction, including without limitation the rights to
 use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 the Software, and to permit persons to whom the Software is furnished to do so,
 subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "ESR.hpp"
vector<Mat_<double> > FernCascade::train(const vector<Mat_<uchar> >& images, const vector<Mat_<double> >& current_shapes, const vector<Mat_<double> >& ground_truth_shapes, const vector<BoundingBox> & bounding_box, const Mat_<double>& mean_shape, int secondLevelNum, int candidate_pixel_num, int fern_pixel_num) {
	Mat_<double> candidate_pixel_locations(candidate_pixel_num, 2);
	Mat_<int> nearest_landmark_index(candidate_pixel_num, 1);
	vector<Mat_<double> > regressionTargets;
	RNG random_generator(getTickCount());
	second_level_num_ = secondLevelNum;

	// calculate regression targets: the difference between ground truth shapes and current shapes
	// candidate_pixel_locations: the locations of candidate pixels, indexed relative to its nearest landmark on mean shape
	regressionTargets.resize(current_shapes.size());
	for (int i = 0; i < current_shapes.size(); i++) {
		regressionTargets[i] = projectShape(ground_truth_shapes[i], bounding_box[i]) - projectShape(current_shapes[i], bounding_box[i]);
		Mat_<double> rotation;
		double scale;
		SimilarityTransform(mean_shape, projectShape(current_shapes[i], bounding_box[i]), rotation, scale);
		transpose(rotation, rotation);	//transpose用来求逆矩阵

		regressionTargets[i] = scale * regressionTargets[i] * rotation;
	}

	// get candidate pixel locations, please refer to 'shape-indexed features'
	for (int i = 0; i < candidate_pixel_num; i++) {
		double x = random_generator.uniform(-1.0, 1.0);
		double y = random_generator.uniform(-1.0, 1.0);
		if (x * x + y * y > 1.0) {
			i--;
			continue;
		}
		// find nearest landmark index
		double min_dist = 1e10;
		int min_index = 0;
		for (int j = 0; j < mean_shape.rows; j++) {
			double temp = pow(mean_shape(j, 0) - x, 2.0) + pow(mean_shape(j, 1) - y, 2.0);
			if (temp < min_dist) {
				min_dist = temp;
				min_index = j;
			}
		}
		candidate_pixel_locations(i, 0) = x - mean_shape(min_index, 0);
		candidate_pixel_locations(i, 1) = y - mean_shape(min_index, 1);
		nearest_landmark_index(i) = min_index;
	}

	// get densities of candidate pixels for each image
	// for densities: each row is the pixel densities at each candidate pixels for an image
	// Mat_<double> densities(images.size(), candidate_pixel_num);
	vector<vector<double> > densities;
	densities.resize(candidate_pixel_num);
	for (int i = 0; i < images.size(); i++) {
		Mat_<double> rotation;
		double scale;
		Mat_<double> temp = projectShape(current_shapes[i], bounding_box[i]);
		SimilarityTransform(temp, mean_shape, rotation, scale);
		for (int j = 0; j < candidate_pixel_num; j++) {
			double project_x = rotation(0, 0) * candidate_pixel_locations(j, 0) + rotation(0, 1) * candidate_pixel_locations(j, 1);
			double project_y = rotation(1, 0) * candidate_pixel_locations(j, 0) + rotation(1, 1) * candidate_pixel_locations(j, 1);
			project_x = scale * project_x * bounding_box[i].width / 2.0;
			project_y = scale * project_y * bounding_box[i].height / 2.0;
			int index = nearest_landmark_index(j);
			int real_x = project_x + current_shapes[i](index, 0);
			int real_y = project_y + current_shapes[i](index, 1);
			real_x = std::max(0.0, std::min((double) real_x, images[i].cols - 1.0));
			real_y = std::max(0.0, std::min((double) real_y, images[i].rows - 1.0));
			densities[j].push_back((int) images[i](real_y, real_x));
		}
	}

	// calculate the covariance between densities at each candidate pixels
	Mat_<double> covariance(candidate_pixel_num, candidate_pixel_num);
	Mat_<double> mean;
	for (int i = 0; i < candidate_pixel_num; i++) {
		for (int j = i; j < candidate_pixel_num; j++) {
			double correlation_result = calculate_covariance(densities[i], densities[j]);
			covariance(i, j) = correlation_result;
			covariance(j, i) = correlation_result;
		}
	}

	// train ferns
	vector<Mat_<double> > prediction;
	prediction.resize(regressionTargets.size());
	for (int i = 0; i < regressionTargets.size(); i++) {
		prediction[i] = Mat::zeros(mean_shape.rows, 2, CV_64FC1);
	}
	ferns_.resize(secondLevelNum);
	for (int i = 0; i < secondLevelNum; i++) {
		cout << "Training ferns: " << i + 1 << " out of " << secondLevelNum << endl;
		vector<Mat_<double> > temp = ferns_[i].Train(densities, covariance, candidate_pixel_locations, nearest_landmark_index, regressionTargets, fern_pixel_num);
		// update regression targets
		for (int j = 0; j < temp.size(); j++) {
			prediction[j] = prediction[j] + temp[j];
			regressionTargets[j] = regressionTargets[j] - temp[j];
		}
	}

	for (int i = 0; i < prediction.size(); i++) {
		Mat_<double> rotation;
		double scale;
		SimilarityTransform(projectShape(current_shapes[i], bounding_box[i]), mean_shape, rotation, scale);
		transpose(rotation, rotation);
		prediction[i] = scale * prediction[i] * rotation;
	}
	return prediction;
}

void FernCascade::read(ifstream& fin) {
	fin >> second_level_num_;		//读入第二级分类器的数量
	ferns_.resize(second_level_num_);
	for (int i = 0; i < second_level_num_; i++) {
		ferns_[i].read(fin);
	}
}

void FernCascade::Write(ofstream& fout) {
	fout << second_level_num_ << endl;
	for (int i = 0; i < second_level_num_; i++) {
		ferns_[i].Write(fout);
	}
}

Mat_<double> FernCascade::predict(const Mat_<uchar>& image, const BoundingBox& bounding_box, const Mat_<double>& mean_shape, const Mat_<double>& shape) {
	Mat_<double> result = Mat::zeros(shape.rows, 2, CV_64FC1);
	Mat_<double> rotation;
	double scale;
	SimilarityTransform(projectShape(shape, bounding_box), mean_shape, rotation, scale);
	for (int i = 0; i < second_level_num_; i++) {
		result = result + ferns_[i].predict(image, shape, rotation, bounding_box, scale);
	}
	SimilarityTransform(projectShape(shape, bounding_box), mean_shape, rotation, scale);
	transpose(rotation, rotation);
	result = scale * result * rotation;

	return result;
}

