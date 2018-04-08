
/*
This source contains the local optimize operations
*/

#include "PrimoMeshViewer.h"
#include <iostream>
#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <Eigen/Eigenvalues>
#include <glm/gtc/quaternion.hpp>

void PrimoMeshViewer::local_optimize(int iterations)
{
	for (int i = 0; i < iterations; i++)
	{
		Mesh::FaceHandle fh;
		// #TODOZQY: take from optimizedFaceHandles_ to get the right face to optimize
		int num_faces = mesh_.n_faces();
		int idx = rand() % num_faces;

		fh = mesh_.face_handle(idx);
		//TODO: optimize for that face
		local_optimize_face(fh);
	}
}

void PrimoMeshViewer::local_optimize_face(Mesh::FaceHandle _fh)
{
	// https://eigen.tuxfamily.org/dox/group__TutorialLinearAlgebra.html
	Eigen::Matrix3f A;
	Eigen::Vector3f b;


	struct EdgePrismData {
		float weight_ij;
		Vec3f centroid_ij;
		Vec3f centroid_ji;
		Vec3f f_ij[4]; // let's store this number as 00,01,10,11
		Vec3f f_ji[4]; // let's store this number as 00,01,10,11
	};

	std::vector<EdgePrismData> f_ij_data;
	Eigen::Matrix4f N; // #TODOZQY: Make sure this is zeroed
	Eigen::Matrix3f S; // #TODOZQY: Make sure this is zeroed
	Vec3f centroid_i(0, 0, 0);
	Vec3f centroid_star(0, 0, 0);
	float weight_sum = 0;
	for (Mesh::FaceHalfedgeCCWIter fhe_ccwiter = mesh_.fh_ccwbegin(_fh); fhe_ccwiter.is_valid(); fhe_ccwiter++)
	{
		// Grab the data to construct the face 
		Mesh::HalfedgeHandle he_ji = mesh_.opposite_halfedge_handle(*fhe_ccwiter);
		Mesh::HalfedgeHandle he_ij = *fhe_ccwiter;
		PrismProperty P_ij = mesh_.property(P_PrismProperty, he_ij);
		PrismProperty P_ji = mesh_.property(P_PrismProperty, he_ji);
		// A simple illustration of how the prism is stored
		/*
		10---------11 ^       10------------11   ^
		| f_ij     |  |       |  f_ji       |    |
		f-he_ij-->to  |       to<--he_ji---from  |
		00---------01 normal  00------------01   normal
		*/
		EdgePrismData Data;
		// Calc face weight
		float edge_len = mesh_.calc_edge_length(*fhe_ccwiter);
		float area_face_i = calc_face_area(_fh); // TODO: calc this
		Mesh::FaceHandle fh_j = mesh_.opposite_face_handle(*fhe_ccwiter);
		float area_face_j = calc_face_area(fh_j); // TODO: calc this
		Data.weight_ij = edge_len / (area_face_i + area_face_j);
		// Calc f_ij
		Data.f_ij[0] = P_ij.f_uv(0, 0, true);
		Data.f_ij[1] = P_ij.f_uv(0, 1, true);
		Data.f_ij[2] = P_ij.f_uv(1, 0, true);
		Data.f_ij[3] = P_ij.f_uv(1, 1, true);
		Data.centroid_ij = (Data.f_ij[0] + Data.f_ij[1] + Data.f_ij[2] + Data.f_ij[3]) * 0.25f;
		// Calc f_ji
		Data.f_ji[0] = P_ji.f_uv(0, 0, false);
		Data.f_ji[1] = P_ji.f_uv(0, 1, false);
		Data.f_ji[2] = P_ji.f_uv(1, 0, false);
		Data.f_ji[3] = P_ji.f_uv(1, 1, false);
		Data.centroid_ij = (Data.f_ji[0] + Data.f_ij[1] + Data.f_ij[2] + Data.f_ji[3]) * 0.25f;
		// I am pretty sure the above can be done with a for loop but not now.

		// Calculate centroids
		centroid_i += Data.centroid_ij * Data.weight_ij;
		centroid_star += Data.centroid_ij * Data.weight_ij;
		weight_sum += Data.weight_ij;

		float root_2s[4] = { 1, 0.5, 0.5, 0.25 }; // TODO : his guy should be global later.
												  // Calculate S value for edge
		for (int i = 0; i < 3; i++)
		{
			for (int j = 0; j < 3; j++)
			{
				// Sum over ijkl 
				float S_ijkl = 0;
				for (int ij = 0; ij < 4; ij++)
				{
					for (int kl = 0; kl < 4; kl++)
					{
						int ijkl_dist = ((~ij) & kl);
						S_ijkl += (Data.f_ij[ij] - Data.centroid_ij)[i] * (Data.f_ji[kl] - Data.centroid_ji)[j] * root_2s[ijkl_dist];
					}
				}
				S(i, j) += S_ijkl / 9.f * Data.weight_ij;
			}
		}
	}
	centroid_i /= weight_sum;
	centroid_star /= weight_sum;

	// Now we have all the S values, time to construct N
	/*
	N =
	| Sxx + Syy + Szz		Syz - Szy			Szx - Sxz			Sxy - Syx		 |
	| Syz - Szy				Sxx - Syy - Szz		Sxy + Syx			Szx + Sxz		 |
	| Szx - Sxz				Sxy + Syx			-Sxx + Syy - Szz	Syz + Szy		 |
	| Sxy - Syx				Szx + Sxz			Syz + Szy			-Sxx - Syy + Szz |
	*/
	const int x = 0;
	const int y = 1;
	const int z = 2;
	N(0, 0) = S(x, x) + S(y, y) + S(z, z);
	N(0, 1) = S(y, z) - S(z, y);
	N(0, 2) = S(z, x) - S(x, z);
	N(0, 3) = S(x, y) - S(y, x);

	N(1, 0) = S(y, z) - S(z, y);
	N(1, 1) = S(x, x) - S(y, y) - S(z, z);
	N(1, 2) = S(x, y) + S(y, x);
	N(1, 3) = S(z, x) + S(x, z);

	N(2, 0) = S(z, x) - S(x, z);
	N(2, 1) = S(x, y) + S(y, x);
	N(2, 2) = -S(x, x) + S(y, y) - S(z, z);
	N(2, 3) = S(y, z) + S(z, y);

	N(3, 0) = S(x, y) - S(y, x);
	N(3, 1) = S(z, x) + S(x, z);
	N(3, 2) = S(y, z) + S(z, y);
	N(3, 3) = -S(x, x) - S(y, y) - S(z, z);

	// Find the largest eigen vector of N and interpret that eigen vector as a rotation quaternion
	//https://eigen.tuxfamily.org/dox/classEigen_1_1EigenSolver.html
	Eigen::EigenSolver<Eigen::Matrix4f> es;
	es.compute(N, true);
	auto eigenvectors = es.eigenvectors();
	Eigen::Matrix<std::complex<float>, 4, 1> eigenvalues = es.eigenvalues();
	std::cout << "We have " << es.eigenvectors().size() << " eigen vectors" << std::endl;

	// Now if we can find that max eigen value -> eigen vector, we get the rotation
	float max_eigen_val = INT_MIN;
	for (int i = 0; i < eigenvalues.rows(); i++)
	{
		// Values 
		if (eigenvalues(i, 0).real() > max_eigen_val)
		{
			max_eigen_val = eigenvalues(i, 0).real();
		}
	}
	int max_col = 0;
	Eigen::Quaternion<float> target_quat;
	for (int col_i = 0; col_i < eigenvectors.cols(); col_i++)
	{
		Eigen::Quaternion<float> rot_quat;
		// #TODOZQY: there must be a better way to fill the quat
		auto eigenvector = eigenvectors.col(col_i);
		rot_quat.coeffs()(0, 0) = eigenvector(0).real() * max_eigen_val;
		rot_quat.coeffs()(1, 0) = eigenvector(1).real() * max_eigen_val;
		rot_quat.coeffs()(2, 0) = eigenvector(2).real() * max_eigen_val;
		rot_quat.coeffs()(3, 0) = eigenvector(3).real() * max_eigen_val;

		auto Nv = (N * eigenvector);

		Eigen::Vector4f diff;
		diff(0) = Nv(0).real() - rot_quat.coeffs()(0, 0);
		diff(1) = Nv(1).real() - rot_quat.coeffs()(1, 0);
		diff(2) = Nv(2).real() - rot_quat.coeffs()(2, 0);
		diff(3) = Nv(3).real() - rot_quat.coeffs()(3, 0);

		if (diff.dot(diff) < 1E-4 && diff.dot(diff) > -1E-4)
		{
			// WE got that 
			std::cout << "hey rot quat is " << eigenvector << std::endl;
			target_quat.x() = eigenvector(0).real();
			target_quat.y() = eigenvector(1).real();
			target_quat.z() = eigenvector(2).real();
			target_quat.w() = eigenvector(3).real();
		}
	}

	// Calculate R, t and c
	Eigen::Vector3f T_i = Eigen::Vector3f(centroid_star[0], centroid_star[1], centroid_star[2]) - target_quat._transformVector(Eigen::Vector3f(centroid_i[0], centroid_i[1], centroid_i[2]));
	std::cout << "Target translation = " << T_i << std::endl;

	// Construct a new transformation, this should be hidden in the implementation tho.
	glm::tquat<float> R(target_quat.x(), target_quat.y(), target_quat.z(), target_quat.w());
	glm::tvec3<float> translation(T_i(0), T_i(1), T_i(2));
	glm::tmat4x4<float> NewTransform();

	// Transformation TargetTransformation(Quaternion(target_quat.x(), target_quat.y(), target_quat.z(), target_quat.w()), Vector3f(T_i(0), T_i(1), T_i(2)));

	Transformation TargetTransformation;

	// Update the prism on each half edge with the new transformation
	for (Mesh::FaceHalfedgeCCWIter fh_ccwit = mesh_.fh_ccwbegin(_fh); fh_ccwit.is_valid(); fh_ccwit++)
	{
		PrismProperty& prop = mesh_.property(P_PrismProperty, *fh_ccwit);
		prop.TransformPrism(TargetTransformation);
	}

	// Update verts, the verts should be the average value of the neighbouring prism verts.
	for (Mesh::FaceVertexCCWIter fv_ccwit = mesh_.fv_ccwbegin(_fh); fv_ccwit.is_valid(); fv_ccwit++)
	{
		// This was the old point calculator
		// I want some noodles
		Vec3f& pt = mesh_.point(*fv_ccwit);
		Eigen::Vector3f NewPos = target_quat._transformVector(Eigen::Vector3f(pt[0], pt[1], pt[2])) + T_i;
		mesh_.point(*fv_ccwit) = Vec3f(NewPos[0], NewPos[1], NewPos[2]);

		// Grab all the vertexes
		Vec3f newPos(0, 0, 0);
		Vec3f newNormal(0, 0, 0);
		int valence = 0;
		// Iterate thru all the half edges that are out going, we should be able to find all the vertex 
		for (Mesh::VertexOHalfedgeCCWIter voh_ccwit = mesh_.voh_ccwbegin(*fv_ccwit); voh_ccwit.is_valid(); voh_ccwit++)
		{
			PrismProperty& prop = mesh_.property(P_PrismProperty, *voh_ccwit);
			Vec3f vert_pt = prop.TargetPosFrom();
			valence++;
			newPos += vert_pt;
		}
		// calculate now position and update to the vertex
		newPos /= valence;
		mesh_.point(*fv_ccwit) = newPos;
		// Question, shall we update the noramls ?
	}
	// Now we updated position, face normals neesd to be updated
	mesh_.update_face_normals();

}