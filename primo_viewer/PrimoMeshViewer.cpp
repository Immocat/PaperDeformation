#include "PrimoMeshViewer.h"
#include <iostream>
#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <Eigen/Eigenvalues>
// #include <glm/gtc/quaternion.hpp>
#include <unordered_set>
#include "lodepng.h"


PrimoMeshViewer::PrimoMeshViewer(const char* _title, int _width, int _height)
	: MeshViewer(_title, _width, _height)
{
	mesh_.request_vertex_colors();

	mesh_.add_property(P_PrismProperty);
	mesh_.add_property(P_globalPrism_intermediate);
	//mesh_.add_property(P_FaceTransformationCache);

	// set color of 3 kind of faces(prisms) here 
	// default: dynamic(orange), 
	dynamicFacesColor_[0] = 1.0f;
	dynamicFacesColor_[1] = 0.64453125f;
	dynamicFacesColor_[2] = 0.0f;
	// static(dark green)
	staticFacesColor_[0] = 0.18039215686f;
	staticFacesColor_[1] = 0.54509803921f;
	staticFacesColor_[2] = 0.34117647058f;
	// optimized(blue)
	optimizedFacesColor_[0] = 0.5294117647f;
	optimizedFacesColor_[1] = 0.80784313725f;
	optimizedFacesColor_[2] = 0.98039215686f;
	
	// do not draw prisms at first
	drawPrisms_ = false;
	global_optimize_iterations_ = 25;
	printf("Gloabl Max Iteration: %d\n", global_optimize_iterations_);

	
	// select mode is STATIC at first
	selectMode_ = ESelectMode::STATIC;
	viewMode_ = EViewMode::VIEW;
	printf("Select Mode: Static\n");
	
	// optimize mode is LOCAL at first
	optimizeMode_ = EOptimizeMode::GLOBAL;
	printf("Optimize Mode: Global\n");

	// do not draw debug info at first
	drawDebugInfo_ = false;
	printf("Draw Debug Info: false\n");

	bKey_space_is_move_ = true;

	print_counter = 0;
}

PrimoMeshViewer::~PrimoMeshViewer()
{

}

bool PrimoMeshViewer::open_mesh(const char* _filename)
{
	if (MeshViewer::open_mesh(_filename))
	{
		// do pre pass of stuff.

		glutPostRedisplay();

		// after successfully opening the mesh, we need firstly calculate average vertices distance
		// prismHeight_ = average vertices distance
		averageVertexDisance_ = get_average_vertex_distance(mesh_);
		prismHeight_ = averageVertexDisance_;

		// init face handles for ray-casting lookup from prim_id to faceHandle
		get_allFace_handles(allFaceHandles_);
		// build bvh for all faces using nanort at first
		build_allFace_BVH();

		// default: all faces are optimizable
		get_allFace_handles(optimizedFaceHandles_);
		update_1typeface_indices(optimizedFaceHandles_, optimizedVertexIndices_);
		for(const OpenMesh::FaceHandle& fh: optimizedFaceHandles_){
			faceIdx_to_selType_[fh.idx()] = ESelectMode::OPTIMIZED;
		}
		// and then, prisms are set up 
		setup_prisms(allFaceHandles_, EPrismExtrudeMode::VERT_NORMAL);
		
		// init the set of idx of opmizedFaces only for global optimization
		for(int i = 0; i <  optimizedFaceHandles_.size(); ++i){
			optimizedFaceIdx_2_i_[optimizedFaceHandles_[i].idx()] = i;
		}
		float initE = E(optimizedFaceHandles_);
		assert(fabs(initE) < FLT_EPSILON);
		return true;
	}
	return false;
}

void PrimoMeshViewer::draw(const std::string& _draw_mode)
{
	if (indices_.empty())
	{
        GlutExaminer::draw(_draw_mode);
        return;
    }

	// 
	Transformation tm;
	add_debug_coordinate(tm, 30.0f);

    if (_draw_mode == "Wireframe")
    {
        glDisable(GL_LIGHTING);
        glColor3f(1.0, 1.0, 1.0);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

        glEnableClientState(GL_VERTEX_ARRAY);
        GL::glVertexPointer(mesh_.points());

        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices_.size()), GL_UNSIGNED_INT, &indices_[0]);

        glDisableClientState(GL_VERTEX_ARRAY);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}
	else if (_draw_mode == "Hidden Line")
	{

	    glDisable(GL_LIGHTING);
	    glShadeModel(GL_SMOOTH);
	    glColor3f(0.3, 0.3, 0.3);

	    glEnableClientState(GL_VERTEX_ARRAY);
	    GL::glVertexPointer(mesh_.points());

	    glDepthRange(0.01, 1.0);
	    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices_.size()), GL_UNSIGNED_INT, &indices_[0]);
	    glDisableClientState(GL_VERTEX_ARRAY);
	    glColor3f(1.0, 1.0, 1.0);

	    glEnableClientState(GL_VERTEX_ARRAY);
	    GL::glVertexPointer(mesh_.points());

	    glDrawBuffer(GL_BACK);
	    glDepthRange(0.0, 1.0);
	    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	    glDepthFunc(GL_LEQUAL);
	    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices_.size()), GL_UNSIGNED_INT, &indices_[0]);

	    glDisableClientState(GL_VERTEX_ARRAY);
	    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	    glDepthFunc(GL_LESS);
	}
	else if (_draw_mode == "Solid Flat")
	{

		Mesh::ConstFaceVertexIter  fv_it;
		glEnable(GL_LIGHTING);
		glShadeModel(GL_FLAT);
		glEnable(GL_COLOR_MATERIAL);
		// draw 3 kind of faces
		glColor3fv(optimizedFacesColor_);
		glBegin(GL_TRIANGLES);
		for (const OpenMesh::FaceHandle& fh : optimizedFaceHandles_)
		{
			GL::glNormal(mesh_.normal(fh));
			fv_it = mesh_.cfv_iter(fh); 
			GL::glVertex(mesh_.point(*fv_it));
			++fv_it;
			GL::glVertex(mesh_.point(*fv_it));
			++fv_it;
			GL::glVertex(mesh_.point(*fv_it));
		}
		glEnd();
		//
		glColor3fv(dynamicFacesColor_);
		glBegin(GL_TRIANGLES);
		for (const OpenMesh::FaceHandle& fh : dynamicFaceHandles_)		
		{
			GL::glNormal(mesh_.normal(fh));
			fv_it = mesh_.cfv_iter(fh); 
			GL::glVertex(mesh_.point(*fv_it));
			++fv_it;
			GL::glVertex(mesh_.point(*fv_it));
			++fv_it;
			GL::glVertex(mesh_.point(*fv_it));
		}
		glEnd();
		//
		glColor3fv(staticFacesColor_);
		glBegin(GL_TRIANGLES);
		for (const OpenMesh::FaceHandle& fh : staticFaceHandles_)	
		{
			GL::glNormal(mesh_.normal(fh));
			fv_it = mesh_.cfv_iter(fh); 
			GL::glVertex(mesh_.point(*fv_it));
			++fv_it;
			GL::glVertex(mesh_.point(*fv_it));
			++fv_it;
			GL::glVertex(mesh_.point(*fv_it));
		}
		glEnd();
		glDisable(GL_COLOR_MATERIAL);
	}
	else if (_draw_mode == "Solid Smooth")
	{
		glEnable(GL_LIGHTING);
		glShadeModel(GL_SMOOTH);
		glEnable(GL_COLOR_MATERIAL);
		
		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_NORMAL_ARRAY);
		GL::glVertexPointer(mesh_.points());
		GL::glNormalPointer(mesh_.vertex_normals());
		// draw 3 type of faces
		if(optimizedVertexIndices_.size()>0)
		{
			glColor3fv(optimizedFacesColor_);
			glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(optimizedVertexIndices_.size()), GL_UNSIGNED_INT, &optimizedVertexIndices_[0]);
		}

		if(dynamicVertexIndices_.size()>0)
		{
			glColor3fv(dynamicFacesColor_);
			glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(dynamicVertexIndices_.size()), GL_UNSIGNED_INT, &dynamicVertexIndices_[0]);
		}
		if(staticVertexIndices_.size() > 0)
		{
			glColor3fv(staticFacesColor_);
			glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(staticVertexIndices_.size()), GL_UNSIGNED_INT, &staticVertexIndices_[0]);
		}
		//
		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_NORMAL_ARRAY);
		glDisable(GL_COLOR_MATERIAL);
	}
	if(drawPrisms_){
		// visualize prisms with wireframes
		glEnable(GL_COLOR_MATERIAL);
		glDisable(GL_LIGHTING);
		glBegin(GL_LINES);
		// draw 3 types of prisms with different color
		glColor3fv(dynamicFacesColor_);
		draw_prisms(dynamicFaceHandles_);
		glColor3fv(staticFacesColor_);
		draw_prisms(staticFaceHandles_);
		glColor3fv(optimizedFacesColor_);
		draw_prisms(optimizedFaceHandles_);
		//
		glEnd();
		glDisable(GL_COLOR_MATERIAL);
	}
	if (drawDebugInfo_)
	{
		glEnable(GL_COLOR_MATERIAL);
		glDisable(GL_LIGHTING);
		draw_debug_lines();
		glDisable(GL_COLOR_MATERIAL);
	}
	// draw dynamic faces rotation axis

	if(dynamicFaceHandles_.size() > 0){
		float prev_line_width;
		glGetFloatv(GL_LINE_WIDTH, &prev_line_width);
		glEnable(GL_COLOR_MATERIAL);
		glDisable(GL_LIGHTING);
		glColor3fv(dynamicFacesColor_);
		glLineWidth(5 * prev_line_width);
		glBegin(GL_LINES);
		const OpenMesh::Vec3f &arrow_from = dynamic_rotation_centroid_;
		OpenMesh::Vec3f arrow_to = arrow_from + dynamic_rotation_axis_ * averageVertexDisance_ * 5;
		glVertex3f(arrow_from[0], arrow_from[1],arrow_from[2]);
		glVertex3f(arrow_to[0], arrow_to[1], arrow_to[2]);
		//
		glEnd();
		glLineWidth(prev_line_width);
		glDisable(GL_COLOR_MATERIAL);
	}

}


void PrimoMeshViewer::keyboard(int key, int x, int y)
{
	// Let super handle us first
	switch (key)
	{
	case 'a':
	case 'A': 
	{
		// toggle if visualize prisms
		drawPrisms_ = !drawPrisms_;
		glutPostRedisplay();
	}
		break;
	case 'd':
	case 'D':
	{
		// toggle if visualize prisms
		drawDebugInfo_ = !drawDebugInfo_;
		glutPostRedisplay();
		printf("Draw Debug Info: %s\n",drawDebugInfo_ ? "true" : "false");
	}
	break;
	case '+':
	{
		// add prisms' height
		prismHeight_ += averageVertexDisance_ * 0.1f;
		// immediately update all prisms, should not use setup_prisms.
		update_prisms_height_uniform(allFaceHandles_, averageVertexDisance_ * 0.1f);
		printf("prismHeight: %f\n", prismHeight_);

		// following the PriMo demo, after changing the prisms' height, we should at once optimize all surface

		thread_pool_.emplace_back([&]() { optimize_faces(optimizedFaceHandles_, optimizedFaceIdx_2_i_, global_optimize_iterations_);});

		glutPostRedisplay();
	}
		break;
	case '-':
	{
		// minus prisms' height(keep prisms' height > 0)
		if(prismHeight_ - averageVertexDisance_ * 0.1f > FLT_EPSILON){
			prismHeight_ -= averageVertexDisance_ * 0.1f;
			// immediately update all prisms, should not use setup_prisms.
			update_prisms_height_uniform(allFaceHandles_, averageVertexDisance_ * -0.1f);
			// following the PriMo demo, after changing the prisms' height, we should at once optimize all surface
			thread_pool_.emplace_back([&]() { optimize_faces(optimizedFaceHandles_, optimizedFaceIdx_2_i_, global_optimize_iterations_);});
		}
		printf("prismHeight: %f\n", prismHeight_);

		
		// based on the new prism height.
		glutPostRedisplay();
	}
		break;
	case '1':
	{
		// select mode changed to STATIC
		// which means that pressing shift + left mouse will select STATIC faces
		printf("Select Mode: Static\n");

		// rebuild nanort bvh for all faces before selecting STATIC/DYNAMIC/OPTIMIZED faces
		if(selectMode_ == ESelectMode::NONE){
			build_allFace_BVH();
		}
		selectMode_ = ESelectMode::STATIC;
	}
		break;
	case '2':
	{
		// select mode changed to DYNAMIC
		// which means that pressing shift + left mouse will select DYNAMIC faces
		printf("Select Mode: Dynamic\n");

		// rebuild nanort bvh for all faces before selecting STATIC/DYNAMIC/OPTIMIZED faces
		if(selectMode_ == ESelectMode::NONE){
			build_allFace_BVH();
		}
		selectMode_ = ESelectMode::DYNAMIC;

	}
		break;
	case '3':
	{
		// select mode changed to OPTIMIZED
		// which means that pressing shift + left mouse will select OPTIMIZED faces
		printf("Select Mode: Optimized\n");

		// rebuild nanort bvh for all faces before selecting STATIC/DYNAMIC/OPTIMIZED faces
		if(selectMode_ == ESelectMode::NONE){
			build_allFace_BVH();
		}
		selectMode_ = ESelectMode::OPTIMIZED;

	}
		break;
	case '4':
	{
		// select mode changed to NONE
		// which means that pressing shift + left mouse and moving mouse will rotate all DYNAMIC faces
		// Also, pressing shift + middle mouse and moving mouse will translate all DYNAMIC faces
		selectMode_ = ESelectMode::NONE;
		printf("Select Mode: None\n"); 
	}
		break;
	case 'o':
	case 'O':
	{
		// switch optimization method (default: global)
		bool opIsLocal = (optimizeMode_ == EOptimizeMode::LOCAL);
		optimizeMode_ = (opIsLocal ? EOptimizeMode::GLOBAL : EOptimizeMode::LOCAL);
		printf("Optimize Mode: %s\n", opIsLocal ? "Global" : "Local");
	}
		break;
	case ' ':
	{
		// 04/18 move all primsms on (optimizedFaceHandles_) to a point ( center_(Currently is the center of the scene) )
		if(selectMode_ != ESelectMode::NONE){
			printf("Deformation could only happen when select mode is NONE\n");
			break;
		}
		if(bKey_space_is_move_){
			squeeze_prisms(optimizedFaceHandles_, center_);

		}
		else{
			thread_pool_.emplace_back([&]() { optimize_faces(optimizedFaceHandles_, optimizedFaceIdx_2_i_, global_optimize_iterations_);});
		}
		// move all optimizable prisms(faces) to a single point(Figure 6 in PriMo paper)
		bKey_space_is_move_  = !bKey_space_is_move_;
		glutPostRedisplay();
	}
		break;
	case 'r':
	case 'R':{
		if(selectMode_ != ESelectMode::NONE){
			printf("Deformation could only happen when select mode is NONE\n");
			break;
		}
		for(const OpenMesh::FaceHandle &fh : staticFaceHandles_){
			optimizedFaceIdx_2_i_[fh.idx()] = optimizedFaceHandles_.size();
			optimizedFaceHandles_.emplace_back(fh);
			faceIdx_to_selType_[fh.idx()] = ESelectMode::OPTIMIZED;
		}
		for(const OpenMesh::FaceHandle &fh : dynamicFaceHandles_){
			optimizedFaceIdx_2_i_[fh.idx()] = optimizedFaceHandles_.size();
			optimizedFaceHandles_.emplace_back(fh);
			faceIdx_to_selType_[fh.idx()] = ESelectMode::OPTIMIZED;
		}
		staticFaceHandles_.clear();
		dynamicFaceHandles_.clear();
		update_1typeface_indices(dynamicFaceHandles_, dynamicVertexIndices_);
		update_1typeface_indices(staticFaceHandles_, staticVertexIndices_);
		update_1typeface_indices(optimizedFaceHandles_, optimizedVertexIndices_);
		glutPostRedisplay();
		thread_pool_.emplace_back([&]() { optimize_faces(optimizedFaceHandles_, optimizedFaceIdx_2_i_, global_optimize_iterations_);});
		glutPostRedisplay();	
	}
		break;
	case 'p':
	case 'P':
	{
		display();
		glutPostRedisplay();
		std::string folder("../../PrimoDemoPresentation/");
		print_counter++;
		std::string filename(std::to_string(print_counter));
		std::string png(".png");
		print_screen(folder+filename+png);
	}
	default:
		GlutExaminer::keyboard(key, x, y);
		break;
	}
}

void PrimoMeshViewer::motion(int x, int y)
{
	switch (viewMode_)
    {
        default:
        case EViewMode::VIEW:
        {
            GlutExaminer::motion(x, y);
            break;
        }
        case EViewMode::MOVE:
        {
            // rotate or translate dynamicFaces when pressing shift + left/middle mouse button 
			// and in ESelect::None mode

			if(selectMode_ == ESelectMode::NONE){
				// rotate or translate dynamicFaces when pressing shift + left/middle mouse button 
				// and in ESelect::None mode
				// rotation
            	if (button_down_[0])
            	{
                	if (last_point_ok_)
                	{
                    	Vec2i  new_point_2D;
                    	Vec3f  new_point_3D;
                    	bool   new_point_ok;

                    	new_point_2D = Vec2i(x, y);
                    	new_point_ok = map_to_sphere(new_point_2D, new_point_3D);

                    	if (new_point_ok)
                    	{
                            float angle = (new_point_2D[0] - last_point_2D_[0]) / (float)width_;
							rotate_faces_and_prisms_around_centroid(dynamic_rotation_centroid_, dynamic_rotation_axis_, angle, dynamicFaceHandles_);
                    	}
                	}
            	}
            	// translation
            	else if (button_down_[1])
            	{
					//printf("button1\n");
					float dist = (last_point_2D_[1] - y) / (float)height_ * averageVertexDisance_ * 1.5f;
					translate_faces_and_prisms_along_axis(dynamic_rotation_axis_, dist, dynamicFaceHandles_);
            	}


            	// remeber points
            	last_point_2D_ = Vec2i(x, y);
            	last_point_ok_ = map_to_sphere(last_point_2D_, last_point_3D_);
            	glutPostRedisplay();
			}
			else{
				raycast_faces(x, y);
				// static int debug_time = 0;
				// printf("ray_cast_motion%d!\n",++debug_time);
			}
            break;
        }
    }
}

void PrimoMeshViewer::mouse(int button, int state, int x, int y)
{
    if( glutGetModifiers() & GLUT_ACTIVE_SHIFT ){
		viewMode_ = EViewMode::MOVE;
		// printf("View Mode: Move\n");
		if(state == GLUT_UP){
			// release button, each mode does different thing
			switch(selectMode_){
				case ESelectMode::DYNAMIC:{
					// 1. ray cast allfaces
					// 2. add to STATIC/DYNAMIC faces based on selectMode_
					// 3. update face STATIC/DYNAMIC indices for drawing
				}
				case ESelectMode::STATIC:{ 
					// 1. ray cast allfaces
					// 2. add to STATIC/DYNAMIC faces based on selectMode_
					// 3. update face STATIC/DYNAMIC indices for drawing
				}
				case ESelectMode::OPTIMIZED:{
					// 1. ray cast allfaces
					// 2. add to STATIC/DYNAMIC faces based on selectMode_
					// 3. update face STATIC/DYNAMIC indices for drawing
					raycast_faces(x, y);
					break;
				}
				case ESelectMode::NONE:{
					// the dynamic faces have been transformed by motion(), minimize all optimizedFaces

					// minimize all optimizedFaces
					thread_pool_.emplace_back([&]() { optimize_faces(optimizedFaceHandles_, optimizedFaceIdx_2_i_, 10);});
					glutPostRedisplay();
					break;
				}
				default:
					assert(false);
					break;
			}
			viewMode_ = EViewMode::VIEW;
		}
	}
    else{
		viewMode_ = EViewMode::VIEW;
		//printf("View Mode: View\n");
	}

	GlutExaminer::mouse(button, state, x, y);
}




void PrimoMeshViewer::raycast_faces(int mouse_x, int mouse_y){
	// 1. ray cast allfaces
	// 1.1 find origin
	//  http://antongerdelan.net/opengl/raycasting.html
	/*  Step 1: 3d Normalised Device Coordinates, range [-1:1, -1:1, -1:1]
		The next step is to transform it into 3d normalised device coordinates. 
		This should be in the ranges of x [-1:1] y [-1:1] and z [-1:1]. 
		We have an x and y already, so we scale their range, and reverse the direction of y.
	*/
	float x = (2.0f * mouse_x) / width_ - 1.0f;
	float y = 1.0f - (2.0f * mouse_y) / height_;
	float z = 1.0f;
	Eigen::Vector3f ray_nds(x, y, z);
	/*  Step 2: 4d Homogeneous Clip Coordinates, range [-1:1, -1:1, -1:1, -1:1]
	    We want our ray's z to point forwards - this is usually the negative z direction in OpenGL style. 
		We can add a w, just so that we have a 4d vector.
		we do not need to reverse perspective division here because this is a ray with no intrinsic depth. 
		We would do that only in the special case of points, for certain special effects.
	*/
	Eigen::Vector4f ray_clip(ray_nds.x(), ray_nds.y(), -1.0, 1.0);
	/*  Step 3: 4d Eye (Camera) Coordinates, range [-x:x, -y:y, -z:z, -w:w]
	    Normally, to get into clip space from eye space we multiply the vector by a projection matrix. 
		We can go backwards by multiplying by the inverse of this matrix.
	*/
	Eigen::Matrix4f proMatrix;
	proMatrix << projection_matrix_[0], projection_matrix_[4], projection_matrix_[8],  projection_matrix_[12],
	             projection_matrix_[1], projection_matrix_[5], projection_matrix_[9],  projection_matrix_[13],
			     projection_matrix_[2], projection_matrix_[6], projection_matrix_[10], projection_matrix_[14],
			     projection_matrix_[3], projection_matrix_[7], projection_matrix_[11], projection_matrix_[15];
	Eigen::Vector4f ray_eye = proMatrix.inverse() * ray_clip;
	/*  Now, we only needed to un-project the x,y part, so let's manually set the z,w part to mean "forwards, and not a point".
	*/
	ray_eye = Eigen::Vector4f(ray_eye.x(), ray_eye.y(), -1.0, 0.0);
	/*  Step 4: 4d World Coordinates, range [-x:x, -y:y, -z:z, -w:w]
	    Same again, to go back another step in the transformation pipeline. 
		Remember that we manually specified a -1 for the z component, which means that our ray isn't normalised. 
		We should do this before we use it.
		This should balance the up-and-down, left-and-right, and forwards components for us. 
		So, assuming our camera is looking directly along the -Z world axis, 
		we should get [0,0,-1] when the mouse is in the centre of the screen, 
		and less significant z values when the mouse moves around the screen. 
		This will depend on the aspect ratio, and field-of-view defined in the view and projection matrices. 
		We now have a ray that we can compare with surfaces in world space.
	*/
	Eigen::Matrix4f viewMatrix;
	viewMatrix << modelview_matrix_[0], modelview_matrix_[4], modelview_matrix_[8],  modelview_matrix_[12],
	              modelview_matrix_[1], modelview_matrix_[5], modelview_matrix_[9],  modelview_matrix_[13],
			      modelview_matrix_[2], modelview_matrix_[6], modelview_matrix_[10], modelview_matrix_[14],
			      modelview_matrix_[3], modelview_matrix_[7], modelview_matrix_[11], modelview_matrix_[15];
	Eigen::Matrix4f viewToWorld(viewMatrix.inverse());
	Eigen::Vector4f ray_dir4 = (viewToWorld * ray_eye);
	Eigen::Vector3f ray_ori3(viewToWorld(0, 3), viewToWorld(1, 3), viewToWorld(2, 3));
	Eigen::Vector3f ray_dir3(ray_dir4.x(),ray_dir4.y(),ray_dir4.z());
	// don't forget to normalise the vector at some point
	ray_dir3.normalize();
	// 1.2 set nanort intersection and traverse bvh
	nanort::TriangleIntersector<float, nanort::TriangleIntersection<float>>
      triangle_intersecter((const float *)mesh_.points(),
	  indices_.data(), sizeof(float) * 3);
	static const float tFar = 1e30;
	nanort::Ray<float> ray;
    ray.min_t = 0.0f;
    ray.max_t = tFar;
    ray.org[0] = ray_ori3[0];
    ray.org[1] = ray_ori3[1];
    ray.org[2] = ray_ori3[2];

    ray.dir[0] = ray_dir3[0];
    ray.dir[1] = ray_dir3[1];
    ray.dir[2] = ray_dir3[2];
    nanort::TriangleIntersection<float> isect;
    bool hit = allFaces_BVH_.Traverse(ray, triangle_intersecter, &isect);
	if(!hit) return;
	// 2. add to STATIC/DYNAMIC faces based on selectMode_
	auto hit_faceIter = faceIdx_to_selType_.find(isect.prim_id);
	assert(hit_faceIter != faceIdx_to_selType_.end());
	bool needUpdateStatic = false;
	bool needUpdateDynamic = false;
	unsigned int hit_faceId = hit_faceIter->first;
	ESelectMode hit_faceType = hit_faceIter->second;
	if(selectMode_ == ESelectMode::STATIC){
		if(hit_faceType == ESelectMode::DYNAMIC){
			delete_faceHandle(hit_faceId, dynamicFaceHandles_);
			needUpdateDynamic = true;
			needUpdateStatic = true;
			staticFaceHandles_.push_back(mesh_.face_handle(hit_faceId));
		}else if(hit_faceType == ESelectMode::OPTIMIZED){
			delete_faceHandle(hit_faceId, optimizedFaceHandles_, &optimizedFaceIdx_2_i_);
			needUpdateStatic = true;
			staticFaceHandles_.push_back(mesh_.face_handle(hit_faceId));
		}
	}else if(selectMode_ == ESelectMode::DYNAMIC){
		if(hit_faceType == ESelectMode::STATIC){
			delete_faceHandle(hit_faceId, staticFaceHandles_);
			needUpdateStatic = true;
			needUpdateDynamic = true;
			dynamicFaceHandles_.push_back(mesh_.face_handle(hit_faceId));
		}else if(hit_faceType == ESelectMode::OPTIMIZED){
			delete_faceHandle(hit_faceId, optimizedFaceHandles_, &optimizedFaceIdx_2_i_);
			needUpdateDynamic = true;
			dynamicFaceHandles_.push_back(mesh_.face_handle(hit_faceId));
		}
	}else if(selectMode_ == ESelectMode::OPTIMIZED){
		if(hit_faceType == ESelectMode::STATIC){
			delete_faceHandle(hit_faceId, staticFaceHandles_);
			needUpdateStatic = true;
			optimizedFaceIdx_2_i_[hit_faceId] = optimizedFaceHandles_.size();
			optimizedFaceHandles_.push_back(mesh_.face_handle(hit_faceId));
			
		}else if(hit_faceType == ESelectMode::DYNAMIC){
			delete_faceHandle(hit_faceId, dynamicFaceHandles_);
			needUpdateDynamic = true;
			optimizedFaceIdx_2_i_[hit_faceId] = optimizedFaceHandles_.size();
			optimizedFaceHandles_.push_back(mesh_.face_handle(hit_faceId));
		}
	}
	else{
		// assertion: raycast_faces could not be called in ESelectMode::NONE mode
		assert(selectMode_ != ESelectMode::NONE); 
	}
	faceIdx_to_selType_[hit_faceId] = selectMode_;

	// 3. update face STATIC/DYNAMIC optimized indices for drawing(That's why they should be update immediatly if necessary)
	if(needUpdateStatic){
		update_1typeface_indices(staticFaceHandles_, staticVertexIndices_);
	}
	if(needUpdateDynamic){
		update_1typeface_indices(dynamicFaceHandles_, dynamicVertexIndices_);
		update_dynamic_rotation_axis_and_centroid();
	}
	if((needUpdateStatic && !needUpdateDynamic) || (!needUpdateStatic && needUpdateDynamic)){
		// need to update optimized when only one type of STATIC/DYNAMIC is changed.
		update_1typeface_indices(optimizedFaceHandles_, optimizedVertexIndices_);
	}
	// assertion for debug face number and indices number
	assert(dynamicFaceHandles_.size() * 3 == dynamicVertexIndices_.size());
	assert(staticFaceHandles_.size() * 3 == staticVertexIndices_.size());
	assert(optimizedFaceHandles_.size() * 3 == optimizedVertexIndices_.size());
	assert(dynamicFaceHandles_.size() + staticFaceHandles_.size() + optimizedFaceHandles_.size()
			== mesh_.n_faces());

	glutPostRedisplay();
}

void PrimoMeshViewer::draw_prisms(const std::vector<OpenMesh::FaceHandle> &face_handles) const{
	// each triangle face has 6 prism vertices and 9 edges
	for (const OpenMesh::FaceHandle& fh : face_handles){
		Mesh::ConstFaceHalfedgeCWIter fh_cwit = mesh_.cfh_cwbegin(fh);
		const Vec3f* pv[6];// 6 vertices of prism
		for (int i = 0; fh_cwit.is_valid(); ++fh_cwit, ++i){
			assert(i < 3);
			const PrismProperty& prop = mesh_.property(P_PrismProperty, *fh_cwit);
			// const Vec3f& from_v = mesh_.point(mesh_.from_vertex_handle(*fh_cwit));
			// pv[i]     = from_v + prop.FromVertPrismDir_DEPRECATED * prop.FromVertPrismSize_DEPRECATED;
			// pv[i + 3] = from_v - prop.FromVertPrismDir_DEPRECATED * prop.FromVertPrismSize_DEPRECATED;
			pv[i] = &(prop.FromVertPrismUp);
			pv[i + 3] = &(prop.FromVertPrismDown);
		}
		// have got all six vertices of prism, draw 9 edges
		// 01, 12, 02, 34, 45, 35, 03, 14, 25
		static const int pv1i[9] = {0, 1, 0, 3, 4, 3, 0, 1, 2};
		static const int pv2i[9] = {1, 2, 2, 4, 5, 5, 3, 4, 5};
		for(int i = 0; i < 9; ++i){
			glVertex3f((*pv[pv1i[i]])[0], (*pv[pv1i[i]])[1], (*pv[pv1i[i]])[2]);
			glVertex3f((*pv[pv2i[i]])[0], (*pv[pv2i[i]])[1], (*pv[pv2i[i]])[2]);
		}
	}
}

void PrimoMeshViewer::update_dynamic_rotation_axis_and_centroid(){
	dynamic_rotation_axis_[0] = 0.0f;
	dynamic_rotation_axis_[1] = 0.0f;
	dynamic_rotation_axis_[2] = 0.0f;
	dynamic_rotation_centroid_[0] = 0.0f;
	dynamic_rotation_centroid_[1] = 0.0f;
	dynamic_rotation_centroid_[2] = 0.0f;
	for(const OpenMesh::FaceHandle& fh: dynamicFaceHandles_){
		dynamic_rotation_axis_ += mesh_.normal(fh);
		for(Mesh::ConstFaceVertexIter cfv_it = mesh_.cfv_begin(fh); cfv_it.is_valid(); ++cfv_it){
			dynamic_rotation_centroid_ += mesh_.point(*cfv_it);
		}
	}
	dynamic_rotation_axis_.normalize();
	if(dynamicFaceHandles_.size() > 0){
		dynamic_rotation_centroid_ /= (float)(dynamicFaceHandles_.size() * 3);
	}
}

void PrimoMeshViewer::print_screen(const std::string& filename) const
{
	GLenum format = GL_RGB;
	GLsizei components = 3;
	GLsizei height = height_;
	GLsizei width = width_;

	unsigned char * data = (unsigned char *)malloc(components * height * width);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadBuffer(GL_FRONT);
	glReadPixels(0, 0, width, height, format, GL_UNSIGNED_BYTE, data);
	std::cout << "second: " << glGetError() << std::endl;
	int dimx = glutGet(GLUT_WINDOW_WIDTH);
	int dimy = glutGet(GLUT_WINDOW_HEIGHT);
	std::cout << "w and h" << height << dimy << std::endl;
	std::vector<unsigned char> gl_buffer(4 * height * width);

	for (int i = 0; i < height; i++)
	{
		for (int j = 0; j < height; j++)
		{
			std::size_t old_pos = (height - i - 1) * (width * 3) + 3 * j;
			std::size_t new_pos = i * (width * 4) + 4 * j;
			gl_buffer[new_pos + 0] = ((unsigned char*)data)[old_pos + 0];
			gl_buffer[new_pos + 1] = ((unsigned char*)data)[old_pos + 1];
			gl_buffer[new_pos + 2] = ((unsigned char*)data)[old_pos + 2];
			gl_buffer[new_pos + 3] = (unsigned char)255;
		}
	}

	std::vector<uint8_t> image_buffer;
	lodepng::encode(image_buffer, gl_buffer, width_, height_);
	std::cout << "saving file to " << filename << std::endl;
	lodepng::save_file(image_buffer, filename);
}

void PrimoMeshViewer::automated_print()
{

}

void PrimoMeshViewer::optimize_faces(const std::vector<OpenMesh::FaceHandle> &face_handles, 
										const std::unordered_map<int,int> &face_idx_2_i, const int max_iterations){
	// here max_iterations is for global optimization, for local we simply *100.
	if(optimizeMode_ == EOptimizeMode::LOCAL){
		local_optimize(face_handles,max_iterations * 100);
	}
	else{
		global_optimize_faces(face_handles, face_idx_2_i, max_iterations);
	}
}

