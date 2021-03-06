#include <OpenMesh/Core/IO/MeshIO.hh>
#include "PrimoMeshViewer.h"
#include <iostream>
#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <Eigen/Eigenvalues>
// #include <glm/gtc/quaternion.hpp>
#include <unordered_set>
#include <string>
#include "CreasePatternParser.h"
#include "pic.h"

//const std::string test_crease_file("../data/curve1.cpx");
bool PrimoMeshViewer::folding_play_ = false;
bool PrimoMeshViewer::folding_record_ = false;
int PrimoMeshViewer::sprite_ = 0;
float PrimoMeshViewer::folding_dAngle_ = 1.0f;
PrimoMeshViewer::PrimoMeshViewer(const char* _title, int _width, int _height)
	: MeshViewer(_title, _width, _height)
{
	mesh_.request_vertex_colors();

	mesh_.add_property(P_PrismProperty);
	mesh_.add_property(P_globalPrism_intermediate);
	// mesh_.add_property(P_collision);
	// mesh_.add_property(P_faceBN);
	//mesh_.add_property(P_FaceTransformationCache);

	// default: optimized(orange)
	opUnitsColor_[0] = 0.6f;
	opUnitsColor_[1] = 0.6f;
	opUnitsColor_[2] = 0.0f;
	
	allFacesColor_[0] = 0.8f;
	allFacesColor_[1] = 0.8f;
	allFacesColor_[2] = 0.8f;

	// do not draw prisms at first
	drawPrisms_ = false;
	global_optimize_iterations_ = 25;
	printf("Gloabl Max Iteration: %d\n", global_optimize_iterations_);

	
	// select mode is STATIC at first
	// selectMode_ = ESelectMode::STATIC;
	//viewMode_ = EViewMode::VIEW;
	//printf("Select Mode: Static\n");
	
	// optimize mode is LOCAL at first
	optimizeMode_ = EOptimizeMode::GLOBAL;
	printf("Optimize Mode: Global\n");

	// do not draw debug info at first
	drawDebugInfo_ = false;
	printf("Draw Debug Info: false\n");

	//folding_angle_ = 0.0f;	
}

PrimoMeshViewer::~PrimoMeshViewer()
{

}

bool PrimoMeshViewer::open_mesh(const char* _filename)
{
	//--------------------------------
	// init a crease parser
	CreasePatternParser parser;
	parser.read_crease_pattern(_filename);
	// this is where the data is processed
	std::vector<std::vector<Mesh::HalfedgeHandle>> crease_hehs;
	std::vector<int> types;
	parser.crease_pattern_to_open_mesh(mesh_, crease_hehs, types);
	//--------------------------------

	//-----------------only for the demo, change all y from 0 to 1 --------------------
	for(Mesh::VertexIter v_iter = mesh_.vertices_begin(); v_iter != mesh_.vertices_end(); ++v_iter){
		mesh_.point(*v_iter)[1] += 1.0f;
	}
	//---------------------------------------------------------------------------------
	for(int i = 0; i < crease_hehs.size(); ++i){
		creases_.emplace_back(crease_hehs[i], mesh_, types[i]);
	}
	// MeshViewer open_mesh
	Mesh::ConstVertexIter  v_it(mesh_.vertices_begin()),
		v_end(mesh_.vertices_end());
	Mesh::Point            bbMin, bbMax;

	bbMin = bbMax = mesh_.point(v_it);
	for (; v_it != v_end; ++v_it)
	{
		bbMin.minimize(mesh_.point(v_it));
		bbMax.maximize(mesh_.point(v_it));
	}
	set_scene((Vec3f)(bbMin + bbMax)*0.5f, 0.5f*(bbMin - bbMax).norm());
	// compute face & vertex normals
	mesh_.update_normals();
	// update face indices for faster rendering
	update_face_indices();
	averageVertexDisance_ = get_average_vertex_distance(mesh_);
	prismHeight_ = averageVertexDisance_;

	// init face handles for ray-casting lookup from prim_id to faceHandle
	get_allFace_handles(allFaceHandles_);

	

	// default: all faces are optimizable
	// get_allFace_handles(optimizedFaceHandles_);
	//update_1typeface_indices(optimizedFaceHandles_, optimizedVertexIndices_);
	// for(const OpenMesh::FaceHandle& fh: optimizedFaceHandles_){
	// 	faceIdx_to_selType_[fh.idx()] = ESelectMode::OPTIMIZED;
	// }
	// and then, prisms are set up 
	setup_prisms(allFaceHandles_, EPrismExtrudeMode::VERT_NORMAL);

	// #TODO[ZJW]:init all optimizeUnits(OpUnits) based on current Creases.
	// given setted creases_ and allFaceHandles_, generate all opUnits
	set_all_opUnits();

	// setup_faceBN(allFaceHandles_);
	// setup_collisionProperty();
	//update_1typeface_indices(allFaceHandles_,allVertexIndices_);
	// init the set of idx of opmizedFaces only for global optimization
	// for(int i = 0; i <  optimizedFaceHandles_.size(); ++i){
	// 	optimizedFaceIdx_2_i_[optimizedFaceHandles_[i].idx()] = i;
	// }
	float initE = E(opUnits_);
	assert(fabs(initE) < FLT_EPSILON);

	// read crease pattern curve
	
	return true;
	// if (MeshViewer::open_mesh(_filename))
	// {
	// 	// do pre pass of stuff.
	// 	// read a crese pattern and override our mesh
	// 	test_read_crease_pattern();
	// 	// after successfully opening the mesh, we need firstly calculate average vertices distance
	// 	// prismHeight_ = average vertices distance
	// 	averageVertexDisance_ = get_average_vertex_distance(mesh_);
	// 	prismHeight_ = averageVertexDisance_;

	// 	// init face handles for ray-casting lookup from prim_id to faceHandle
	// 	get_allFace_handles(allFaceHandles_);

	// 	// default: all faces are optimizable
	// 	get_allFace_handles(optimizedFaceHandles_);
	// 	//update_1typeface_indices(optimizedFaceHandles_, optimizedVertexIndices_);
	// 	// for(const OpenMesh::FaceHandle& fh: optimizedFaceHandles_){
	// 	// 	faceIdx_to_selType_[fh.idx()] = ESelectMode::OPTIMIZED;
	// 	// }
	// 	// and then, prisms are set up 
	// 	setup_prisms(allFaceHandles_, EPrismExtrudeMode::VERT_NORMAL);
	// 	setup_faceBN(allFaceHandles_);
	// 	// setup_collisionProperty();
	// 	//update_1typeface_indices(allFaceHandles_,allVertexIndices_);
	// 	// init the set of idx of opmizedFaces only for global optimization
	// 	for(int i = 0; i <  optimizedFaceHandles_.size(); ++i){
	// 		optimizedFaceIdx_2_i_[optimizedFaceHandles_[i].idx()] = i;
	// 	}
	// 	float initE = E(optimizedFaceHandles_);
	// 	assert(fabs(initE) < FLT_EPSILON);

	// 	// read crease pattern curve
		
	// 	return true;
	// }
	// return false;
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
		// only draw all foldable faces 
		Mesh::ConstFaceVertexIter  fv_it;
		glEnable(GL_LIGHTING);
		glShadeModel(GL_FLAT);
		glEnable(GL_COLOR_MATERIAL);
		// draw 3 kind of faces
		// glColor3fv(notOptimizaedFacesColor_);
		// glBegin(GL_TRIANGLES);
		// for (const OpenMesh::FaceHandle& fh : not_optimizedFaceHandles_)
		// {
		// 	GL::glNormal(mesh_.normal(fh));
		// 	fv_it = mesh_.cfv_iter(fh); 
		// 	GL::glVertex(mesh_.point(*fv_it));
		// 	++fv_it;
		// 	GL::glVertex(mesh_.point(*fv_it));
		// 	++fv_it;
		// 	GL::glVertex(mesh_.point(*fv_it));
		// }
		// glEnd();
		
		glBegin(GL_TRIANGLES);
		for(const Crease &crease : creases_){
			crease.draw_falt_foldable_faces(P_PrismProperty); 
		}
		glColor3fv(allFacesColor_);
		for (const OpenMesh::FaceHandle& fh : allFaceHandles_)
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
		// draw all faces in solid smooth mode
		glColor3fv(allFacesColor_);
		glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices_.size()), GL_UNSIGNED_INT, &indices_[0]);

		// if(dynamicVertexIndices_.size()>0)
		// {
		// 	glColor3fv(dynamicFacesColor_);
		// 	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(dynamicVertexIndices_.size()), GL_UNSIGNED_INT, &dynamicVertexIndices_[0]);
		// }
		// if(staticVertexIndices_.size() > 0)
		// {
		// 	glColor3fv(staticFacesColor_);
		// 	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(staticVertexIndices_.size()), GL_UNSIGNED_INT, &staticVertexIndices_[0]);
		// }
		//
		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_NORMAL_ARRAY);
		glDisable(GL_COLOR_MATERIAL);
	}
	// draw mountain and valley edges
	float prev_line_width;
	glGetFloatv(GL_LINE_WIDTH, &prev_line_width);
	glLineWidth(3 * prev_line_width);
	glEnable(GL_COLOR_MATERIAL);
	glDisable(GL_LIGHTING);
	glBegin(GL_LINES);
	for(const Crease &crease : creases_){
		crease.draw();
	}
	glEnd();
	glDisable(GL_COLOR_MATERIAL);
	glLineWidth(prev_line_width);
	if(drawPrisms_){
		// visualize prisms of all OpUnit boundary prisms
		glEnable(GL_COLOR_MATERIAL);
		glDisable(GL_LIGHTING);
		
 
		glBegin(GL_LINES);
		for(const Crease &crease : creases_){
			crease.draw_prisms(P_PrismProperty);
		}
		glEnd();

		// glBegin(GL_LINES);
		// glColor3fv(opUnitsColor_);
		// for(const OpUnit &opUnit : opUnits_){
		// 	opUnit.draw_prism(P_PrismProperty);
		// }
		// glEnd();
		glDisable(GL_COLOR_MATERIAL);
	}
	if (drawDebugInfo_)
	{
		glEnable(GL_COLOR_MATERIAL);
		glDisable(GL_LIGHTING);
		//draw_debug_lines();
		// only draw vertex normal now
		glColor3f(0.0f, 1.0f, 0.0f);
		glBegin(GL_LINES);
		for(Mesh::VertexIter v_iter = mesh_.vertices_begin(); v_iter != mesh_.vertices_end(); ++v_iter){
			const Mesh::Point &start = mesh_.point(*v_iter);
			Mesh::Point dir = mesh_.normal(*v_iter);
			dir *= averageVertexDisance_;
			const Mesh::Point end = start + dir;
			glVertex3f(start[0], start[1], start[2]);
			glVertex3f(end[0], end[1], end[2]);
		}
		glEnd();
		glDisable(GL_COLOR_MATERIAL);
	}
	// draw dynamic faces rotation axis

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

		optimize_faces(opUnits_, optimizedFaceIdx_2_opUnits_i, global_optimize_iterations_);

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
			optimize_faces(opUnits_, optimizedFaceIdx_2_opUnits_i, global_optimize_iterations_);
		}
		printf("prismHeight: %f\n", prismHeight_);

		
		// based on the new prism height.
		glutPostRedisplay();
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
	case 'c':
	case 'C':{
		// read Dynamic Crease Configuration(DCC) file
		// including: 
		// crease index, 0/1 folding or not, prism height percentegate(0.2 measn 0.2 * default height)
		
		// Assertion: only load DCC file after triangulation
		assert(creases_.size() > 0);
		std::string dcc_filename;
		std::cout<<"Please type in the Dynamic Crease Configuration(DCC) file\n";
		std::cin>> dcc_filename;
		if(!read_dcc_file(dcc_filename)){
			std::cout<<"Read DCC file failed!\n";
			break;
		}
		//
	}
		break;
	case 'f':
	case 'F':{
		// forward folding
		//folding_angle_ += 0.5;
		for(Crease &crease : creases_){
			crease.fold(folding_dAngle_, P_PrismProperty);
		}
		optimize_faces(opUnits_, optimizedFaceIdx_2_opUnits_i, global_optimize_iterations_);
		//thread_pool_.emplace_back([&]() { optimize_faces(opUnits_, optimizedFaceIdx_2_opUnits_i, global_optimize_iterations_);});
		glutPostRedisplay();
	}
		break;
	case 'b':
	case 'B':{
		folding_dAngle_ *= -1.0f;
		printf("folding_dAngle_: %f\n", folding_dAngle_);
	}
		break;
	case 'r':
	case 'R':{
		// used for recording the final vedio
		folding_record_ = !folding_record_;
		folding_play_ = !folding_play_; 
	}
		break;
	case 'p':
	case 'P':{
		// used for real time demo
		folding_play_ = !folding_play_; 
	}
		break;
	default:
		GlutExaminer::keyboard(key, x, y);
		break;
	}
}

// void PrimoMeshViewer::draw_prisms(const std::vector<OpenMesh::FaceHandle> &face_handles) const{
// 	// each triangle face has 6 prism vertices and 9 edges
// 	for (const OpenMesh::FaceHandle& fh : face_handles){
// 		Mesh::ConstFaceHalfedgeCWIter fh_cwit = mesh_.cfh_cwbegin(fh);
// 		const Vec3f* pv[6];// 6 vertices of prism
// 		for (int i = 0; fh_cwit.is_valid(); ++fh_cwit, ++i){
// 			assert(i < 3);
// 			const PrismProperty& prop = mesh_.property(P_PrismProperty, *fh_cwit);
// 			// const Vec3f& from_v = mesh_.point(mesh_.from_vertex_handle(*fh_cwit));
// 			// pv[i]     = from_v + prop.FromVertPrismDir_DEPRECATED * prop.FromVertPrismSize_DEPRECATED;
// 			// pv[i + 3] = from_v - prop.FromVertPrismDir_DEPRECATED * prop.FromVertPrismSize_DEPRECATED;
// 			pv[i] = &(prop.FromVertPrismUp);
// 			pv[i + 3] = &(prop.FromVertPrismDown);
// 		}
// 		// have got all six vertices of prism, draw 9 edges
// 		// 01, 12, 02, 34, 45, 35, 03, 14, 25
// 		static const int pv1i[9] = {0, 1, 0, 3, 4, 3, 0, 1, 2};
// 		static const int pv2i[9] = {1, 2, 2, 4, 5, 5, 3, 4, 5};
// 		for(int i = 0; i < 9; ++i){
// 			glVertex3f((*pv[pv1i[i]])[0], (*pv[pv1i[i]])[1], (*pv[pv1i[i]])[2]);
// 			glVertex3f((*pv[pv2i[i]])[0], (*pv[pv2i[i]])[1], (*pv[pv2i[i]])[2]);
// 		}
// 	}
// }


void PrimoMeshViewer::optimize_faces(std::vector<OpUnit> &opUnits, 
										const std::unordered_map<int,int> &face_idx_2_i, const int max_iterations){
	// here max_iterations is for global optimization, for local we simply *100.
	if(optimizeMode_ == EOptimizeMode::LOCAL){
		local_optimize(opUnits,max_iterations * 100);
	}
	else{
		global_optimize_faces(opUnits, face_idx_2_i, max_iterations);
	}
}
void PrimoMeshViewer::saveScreenshot(int windowWidth, int windowHeight, char *filename)
{
	if (filename == NULL)
		return;

	// Allocate a picture buffer 
	Pic * in = pic_alloc(windowWidth, windowHeight, 3, NULL);

	printf("File to save to: %s\n", filename);

	for (int i = windowHeight - 1; i >= 0; i--)
	{
		glReadPixels(0, windowHeight - i - 1, windowWidth, 1, GL_RGB, GL_UNSIGNED_BYTE,
			&in->pix[i*in->nx*in->bpp]);
	}

	if (ppm_write(filename, in))
		printf("File saved Successfully\n");
	else
		printf("Error in Saving\n");

	pic_free(in);
}

void PrimoMeshViewer::save_mesh_to_obj(const std::string & filename)
{
	if (!OpenMesh::IO::write_mesh(mesh_, filename))
	{
		std::cerr << "Write mesh to " << filename << " failed!" << std::endl;
	}
}

void PrimoMeshViewer::idle(){
	// char s[20] = "xxxx.ppm";

	if (folding_play_){
		if(folding_record_){
			std::string base_name;
			base_name += (char)(48 + (sprite_ % 10000) / 1000);
			base_name += (char)(48 + (sprite_ % 1000) / 100);
			base_name += (char)(48 + (sprite_ % 100) / 10);
			base_name += (char)(48 + sprite_ % 10);
			std::string obj_name;
			obj_name += "../obj/";
			obj_name += base_name;
			obj_name += ".obj";
			std::string ppm_name = "../ppm/" + base_name + ".ppm";
			char * charx = (char*)(ppm_name.c_str());
			saveScreenshot(width_, height_, charx);
			save_mesh_to_obj(obj_name);
		}
		for(Crease &crease : creases_){
			crease.fold(folding_dAngle_, P_PrismProperty);
		}
		optimize_faces(opUnits_, optimizedFaceIdx_2_opUnits_i, global_optimize_iterations_);
		glutPostRedisplay();
		++sprite_;
	}
	
	if (sprite_ >= 90) // allow only 90 degree folding now
	{
		sprite_ = 0;
		folding_record_ = false;
		folding_play_ = false;
	}
	glutPostRedisplay();
}