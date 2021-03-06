#include "PrimoMeshViewer.h"
#include "Timer.hh"
#include <Eigen/Sparse>
#include <unordered_set>
typedef Eigen::SparseMatrix<float> SpMat;
typedef OpenMesh::TriMesh_ArrayKernelT<>  Mesh;
struct PijKey{
    int p[2];
    PijKey(const int &a, const int &b){
        p[0] = a;
        p[1] = b;
    }
    int& operator[](unsigned int i) { return p[i]; }
    const int& operator[](unsigned int i) const { return p[i]; }
    bool operator==(const PijKey &other) const
    {
        return (p[0] == other.p[0]
            && p[1] == other.p[1]
            );
    }
};
namespace std {

  template <>
  struct hash<PijKey>{
    // boost::hash_combine for pair int's hash
    std::size_t operator()(const PijKey& p) const{
        size_t seed = 0;
        hash<int> h;
        seed ^= h(p[0]) + 0x9e3779b9 + (seed << 6) + (seed >> 2); 
        seed ^= h(p[1]) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
  };

}

// helper class to memoization for pij and pji
class Pij_ji{
public:
    explicit Pij_ji(const PrismProperty * const P_i, const PrismProperty * const P_j):
    f_ij{       P_i->f_uv(0, 0, true) ,P_i->f_uv(0, 1, true),
                P_i->f_uv(1, 0, true), P_i->f_uv(1, 1, true)
    },
    f_ji{       P_j->f_uv(0, 0, false), P_j->f_uv(0, 1, false),
                P_j->f_uv(1, 0, false), P_j->f_uv(1, 1, false)
    }, one{
        {1, 1, 1}, {1, 1, 1},
        {1, 1, 1}, {1, 1, 1}
    }
    {}
    float operator()(const int first, const int second){ 
        // assert it is a valid key
        // pij(x, y, z)   pji(x, y, z)  one(1,  1,  1)
        //     0  1  2        3  4  5      -1  -1  -1
        assert(first >= -1 && first <= 5 && second >= -1 && second <= 5 && first <= second);
        // firstly check if the key is already calculated
        PijKey key{first, second};
        auto iter = p.find(key);
        if(iter != p.end()){
            // this pair has already been calculated, return directly
            return iter->second;
        }else{
            // the (key,value) is not in the map
            // 1. calculate it 
            float value = calc_value(first, second);
            // 2. save it to the map
            p.emplace(key, value);
            // return it
            return value;
        }

    }
    // private data
private:
    std::unordered_map<PijKey, float> p;
    const OpenMesh::Vec3f f_ij[4];
    const OpenMesh::Vec3f f_ji[4];
    // dummy one vectors for easy implementation
    const OpenMesh::Vec3f one[4];
    //
    float calc_value(int first, int second) const{
        // firstly sort two integer small, big
        // cannot have two "one"
        assert(second >= 0);
        // special case when first is -1
        // iterate each combination
        const OpenMesh::Vec3f* first_array;
        const OpenMesh::Vec3f* second_array;
        if(first < 0){
            first_array = one;
            first = 0;
        }else if(first < 3){
            first_array = f_ij;
        }else{
            first_array = f_ji;
            first -= 3;
        }
        
        if(second < 3){
            second_array = f_ij;
        }else{
            second_array = f_ji;
            second -= 3;
        }
        if(first_array == one){
            return (second_array[0][second] + second_array[1][second] + second_array[2][second] + second_array[3][second]) * 0.25f;
        }
        float value = 0.0;//result
        static const float one_ninth = 1.0f/9.0f;
        static const float root_2s[4] = { one_ninth*1, one_ninth * 0.5, one_ninth * 0.5, one_ninth * 0.25 }; 
		for (int ij = 0; ij < 4; ij++)
		{
			for (int kl = 0; kl < 4; kl++)
			{
						// int ijkl_dist = ((~ij) & kl);
				int ijkl_dist = (ij) ^ (kl);
				value += (first_array[ij])[first] * (second_array[kl])[second] * root_2s[ijkl_dist];
			}
		}

        return value;
    }
    
};
class BFill{
public:
    explicit BFill(int n):B_n(n){}
    float& operator()(const int r, const int c){ 
        // assert it is a valid key
        // pij(x, y, z)   pji(x, y, z)  one(1,  1,  1)
        //     0  1  2        3  4  5      -1  -1  -1
        assert(r >= 0 && r < B_n && c >= 0 && c < B_n);
        // firstly check if the key is already calculated
        PijKey key{r, c};
        auto iter = p.find(key);
        if(iter != p.end()){
            // this pair has already been calculated, return directly
            return iter->second;
        }else{
            auto iter_bool = p.emplace(key, 0.0f);
            assert(iter_bool.second == true);
            return iter_bool.first->second;
        }

    }
    void operator+=(const BFill &other){
        for(const auto &element : other.p){
            this->operator()(element.first[0],element.first[1]) += element.second;
        }
    }
public:
    std::unordered_map<PijKey, float> p;
    const int B_n;

};
static void build_problem_Eigen(const int n6, const Mesh &mesh, const OpenMesh::HPropHandleT<PrismProperty> &P_PrismProperty, 
                            const std::vector<OpenMesh::FaceHandle> &face_handles, const std::unordered_map<int,int> &face_idx_2_i, 
                            SpMat &B, 
                            Eigen::VectorXf &negA_T){
    BFill bFill(n6);
    std::unordered_set<int> he_id_set;
    //////////////////////////////////////////////////////////////////////////////
    //std::cout<< "B:\n" <<  B <<std::endl;
    //std::cout<< "-A^T:\n" << negA_T << std::endl;
    // double plus_equal_duration = 0.0;
    //////////////////////////////////////////////////////////////////////////////
    for(int i = 0; i < face_handles.size(); ++i){
        // iterate all faces
        const OpenMesh::FaceHandle &fh_i = face_handles[i];
        const int f_i_id = fh_i.idx();
        for(Mesh::ConstFaceHalfedgeIter fhe_it = mesh.cfh_iter(fh_i); fhe_it.is_valid(); ++fhe_it){
            // Grab the opposite half edge and face
            const Mesh::HalfedgeHandle he_i = *fhe_it;
            Mesh::HalfedgeHandle he_j = mesh.opposite_halfedge_handle(he_i);
		    Mesh::FaceHandle fh_j = mesh.opposite_face_handle(*fhe_it);
            // if he_i is a boundary halfedge, there is no opposite prism face,
            // which means that it has no contribution to the total energy E = Sum(w_ij * E_ij)
            // we could skip this half edge he_i now
            if (!he_j.is_valid() || mesh.is_boundary(he_i) || !fh_j.is_valid()){
                #ifndef NDEBUG
			    std::cout << "halfedge's opposite doesnot exist, idx = "<< he_i.idx() << std::endl;
                #endif
			    continue;
		    }

            // for same pair half edge Eij = Eji, we only want to calculate it once
            // (he_i, he_j) == (he_j, he_i)
            const int he_i_id = he_i.idx();
            const int he_j_id = he_j.idx();
            
            const bool he_i_in_set = (he_id_set.find(he_i_id) != he_id_set.end());
            if(he_i_in_set){
                #ifndef NDEBUG
                const bool he_j_in_set = (he_id_set.find(he_j_id) != he_id_set.end());
                // assert for debug, he pair should be both in set or neither in set 
                assert(he_i_in_set && he_j_in_set);
                #endif
                continue;
            }
            // this pair is visited now, put them into set
            he_id_set.insert(he_i_id);
            he_id_set.insert(he_j_id);

            // get the f^ij_[0/1][0/1] in the PriMo equation
            const int f_j_id = fh_j.idx();
            const PrismProperty * const P_i = &(mesh.property(P_PrismProperty, he_i));
            const PrismProperty * const P_j = &(mesh.property(P_PrismProperty, he_j));
            const float w_ij = P_i->weight_ij * 0.1f;
            // assert for debug, opposite half edges should have same edge weight
            assert(fabs(w_ij - P_j->weight_ij) < FLT_EPSILON);

            // get all p^ij p^ji that are used to fill in matrices
            Pij_ji p(P_i, P_j);
            
            // boundary contidion: opposite face is not optimizable,
            // which has a simpler method to fill in matrices more efficiently
            // pij(x, y, z)   pji(x, y, z)  one(1,  1,  1)
            //     0  1  2        3  4  5      -1  -1  -1
            //SpMat current_B(n6, n6);
            //SpMat current_negA_T(n6, 1);
            Eigen::Vector3f N, M;
            N << p(-1, 0) - p(-1, 3), p(-1, 1) - p(-1, 4), p(-1, 2) - p(-1, 5);
            M << -p(2,4)+p(1,5), p(2,3)-p(0,5), -p(1,3)+p(0,4);
            M *= w_ij;
            N *= w_ij;
            if(face_idx_2_i.find(f_j_id) == face_idx_2_i.end()){
                // boundary case
                Eigen::Matrix<float, 6, 6> DTD; 
                DTD<< 
                p(1,1)+p(2,2),           -p(0,1),        -p(0,2),                 0,         -p(-1,2),          p(-1,1),
                            0,     p(0,0)+p(2,2),        -p(1,2),           p(-1,2),                0,         -p(-1,0),
                            0,                 0,  p(0,0)+p(1,1),          -p(-1,1),          p(-1,0),                0,
                            0,                 0,              0,                 1,                0,                0,
                            0,                 0,              0,                 0,                1,                0,
                            0,                 0,              0,                 0,                0,                1;
                DTD *= w_ij;
                // copy the upper part to become a symmetric matrix
                //std::cout<<"DTD before\n"<<DTD<<std::endl;
                DTD = DTD.selfadjointView<Eigen::Upper>();
                //std::cout<<"DTD after\n"<<DTD<<std::endl;

                //////////////////////////////////////////////////////////////////////////////
                // std::cout<< "boundary DTD:\n" <<  DTD <<std::endl;
                //////////////////////////////////////////////////////////////////////////////
                // push DTD into current_B
                // current_B.reserve(Eigen::VectorXi::Constant(n6, 6));
                const int start_rc = i * 6;
                const int end_rc   = start_rc + 6;
                for(int r = start_rc, dtd_i = 0; dtd_i < 6; ++r, ++dtd_i){
                    for(int c = start_rc, dtd_j = 0; dtd_j < 6; ++c, ++dtd_j){
                        bFill(r, c) += DTD(dtd_i, dtd_j);
                    }
                }
                // init current_negA_T
                negA_T(start_rc    ) += M(0); 
                negA_T(start_rc + 1) += M(1); 
                negA_T(start_rc + 2) += M(2); 
                negA_T(start_rc + 3) -= N(0); 
                negA_T(start_rc + 4) -= N(1); 
                negA_T(start_rc + 5) -= N(2);
            }else{
                // general case
                Eigen::Matrix<float, 12, 12> DTD;
                DTD<<
                p(1,1)+p(2,2),           -p(0,1),        -p(0,2),                 0,         -p(-1,2),          p(-1,1),   -p(1,4)-p(2,5),          p(1,3),             p(2,3),                0,          p(-1,2),         -p(-1,1),
                            0,     p(0,0)+p(2,2),        -p(1,2),           p(-1,2),                0,         -p(-1,0),           p(0,4),  -p(0,3)-p(2,5),             p(2,4),         -p(-1,2),                0,          p(-1,0),
                            0,                 0,  p(0,0)+p(1,1),          -p(-1,1),          p(-1,0),                0,           p(0,5),          p(1,5),     -p(0,3)-p(1,4),          p(-1,1),         -p(-1,0),                0,
                            0,                 0,              0,                 1,                0,                0,                0,        -p(-1,5),            p(-1,4),               -1,                0,                0,    
                            0,                 0,              0,                 0,                1,                0,          p(-1,5),               0,           -p(-1,3),                0,               -1,                0,
                            0,                 0,              0,                 0,                0,                1,         -p(-1,4),         p(-1,3),                  0,                0,                0,               -1,
                            0,                 0,              0,                 0,                0,                0,    p(4,4)+p(5,5),         -p(3,4),            -p(3,5),                0,         -p(-1,5),          p(-1,4),
                            0,                 0,              0,                 0,                0,                0,                0,   p(3,3)+p(5,5),            -p(4,5),          p(-1,5),                0,         -p(-1,3),
                            0,                 0,              0,                 0,                0,                0,                0,               0,      p(3,3)+p(4,4),         -p(-1,4),          p(-1,3),                0,
                            0,                 0,              0,                 0,                0,                0,                0,               0,                  0,                1,                0,                0,                                        
                            0,                 0,              0,                 0,                0,                0,                0,               0,                  0,                0,                1,                0,            
                            0,                 0,              0,                 0,                0,                0,                0,               0,                  0,                0,                0,                1;

                DTD *= w_ij;

                // copy the upper part to become a symmetric matrix
                DTD = DTD.selfadjointView<Eigen::Upper>();
                
                //////////////////////////////////////////////////////////////////////////////
                //std::cout<< "DTD:\n" <<  DTD <<std::endl;
                //////////////////////////////////////////////////////////////////////////////
                // push DTD into current_B
                // current_B.reserve(Eigen::VectorXi::Constant(n6, 12));
                const int start_I = i * 6;
                //const int   end_I = start_I + 6;
                const int start_J = face_idx_2_i.at(f_j_id) * 6;
                //const int   end_J = start_J + 6;

                for(int r = start_I, dtd_i = 0; dtd_i < 6; ++r, ++dtd_i){
                    for(int c = start_I, dtd_j = 0; dtd_j < 6; ++c, ++dtd_j){
                        bFill(r, c) += DTD(dtd_i, dtd_j);
                    }
                }

                for(int r = start_I, dtd_i = 0; dtd_i < 6; ++r, ++dtd_i){
                    for(int c = start_J, dtd_j = 6; dtd_j < 12; ++c, ++dtd_j){
                        bFill(r, c) += DTD(dtd_i, dtd_j);
                    }
                }
                for(int r = start_J, dtd_i = 6; dtd_i < 12; ++r, ++dtd_i){
                    for(int c = start_I, dtd_j = 0; dtd_j < 6; ++c, ++dtd_j){
                        bFill(r, c) += DTD(dtd_i, dtd_j);
                    }
                }
                for(int r = start_J, dtd_i = 6; dtd_i < 12; ++r, ++dtd_i){
                    for(int c = start_J, dtd_j = 6; dtd_j < 12; ++c, ++dtd_j){
                        bFill(r, c) += DTD(dtd_i, dtd_j);
                    }
                }
                // init current_negA_T
                negA_T(start_I    ) += M(0); 
                negA_T(start_I + 1) += M(1); 
                negA_T(start_I + 2) += M(2); 
                negA_T(start_I + 3) -= N(0); 
                negA_T(start_I + 4) -= N(1); 
                negA_T(start_I + 5) -= N(2);

                negA_T(start_J    ) -= M(0); 
                negA_T(start_J + 1) -= M(1); 
                negA_T(start_J + 2) -= M(2); 
                negA_T(start_J + 3) += N(0); 
                negA_T(start_J + 4) += N(1); 
                negA_T(start_J + 5) += N(2);  

            }

        }
    }
    //////////////////////////////////////////////////////////////////////////////
    for(const auto &element : bFill.p){
        B.insert(element.first[0],element.first[1]) = element.second;
    }
    // std::cout<<"B\n"<<B<<std::endl;
    // std::cout<<"negAT\n"<<negA_T<<std::endl;
    //////////////////////////////////////////////////////////////////////////////
    
}
void PrimoMeshViewer::project_v_and_update_prisms(const Eigen::VectorXf &C, const std::vector<OpenMesh::FaceHandle> &face_handles, float lambda){
    // 
    for(int i = 0; i < face_handles.size();++i){
        float weight_sum = 0;
        const OpenMesh::FaceHandle &fh_i = face_handles[i];
        const int i6 = i * 6;
        const OpenMesh::Vec3f wi(C(i6), C(i6 + 1), C(i6 + 2));
        const OpenMesh::Vec3f vi(C(i6 + 3), C(i6 + 4), C(i6 + 5));
        for(Mesh::ConstFaceHalfedgeIter fh_iter = mesh_.cfh_begin(fh_i); fh_iter.is_valid(); ++fh_iter){
            PrismProperty &dst = mesh_.property(P_globalPrism_intermediate,*fh_iter);
            const PrismProperty &src = mesh_.property(P_PrismProperty,*fh_iter);
            dst = src;
            dst.FromVertPrismDown += lambda * (OpenMesh::cross(wi, dst.FromVertPrismDown) + vi);
            dst.FromVertPrismUp += lambda * (OpenMesh::cross(wi, dst.FromVertPrismUp) + vi);
            dst.ToVertPrismDown += lambda * (OpenMesh::cross(wi, dst.ToVertPrismDown) + vi);
            dst.ToVertPrismUp += lambda * (OpenMesh::cross(wi, dst.ToVertPrismUp) + vi);
        }
    }
    for(const OpenMesh::FaceHandle &fh:face_handles){
        local_optimize_face(fh, P_globalPrism_intermediate, true);
    }
}
void PrimoMeshViewer::global_optimize_faces(const std::vector<OpenMesh::FaceHandle> &face_handles, 
										const std::unordered_map<int,int> &face_idx_2_i, const int max_iterations)
{
    /* build the linear system. Here we follow the convention in 
       [PLH02]:http://citeseerx.ist.psu.edu/viewdoc/download;jsessionid=F1D81AF3257335BC6E902048DF161317?doi=10.1.1.6.5569&rep=rep1&type=pdf
        B C = -A^T. #TODO[ZJW]: add link of supplemental notes for explanation
    */
    // debug for that face_handles and face_idx_2_i data matches
    // do nothing is no optimizable face
    if(face_handles.size() <= 0) return;
    #ifndef NDEBUG
        std::cout << "face_handles size:" << face_handles.size() << std::endl;
        std::cout << "face_idx_2_i size:" << face_idx_2_i.size() << std::endl;
        assert(face_handles.size() == face_idx_2_i.size());
		for(int i = 0; i < face_handles.size(); ++i){
            // int idx = face_handles[i].idx();
            assert(face_idx_2_i.at(face_handles[i].idx()) == i);
        }
    #endif
    // Timer total_timer;
    int n6 = (int)face_handles.size() * 6;
    // -A^T ("b" in "Ax = b"), it is init to zero.
    float E_k = 0.0f, E_km1 = E(face_handles);
    // std::cout<<"E_origin: "<<E_origin<<std::endl;
    float lambda = 1.0f;
    for(int i = 0; i < max_iterations; ++i, lambda *= 0.95f){
        // find optimal velocities
        Timer solve_linear_system_timer;
        Eigen::VectorXf negA_T = Eigen::VectorXf::Zero(n6);
        SpMat B(n6, n6);
        B.reserve(Eigen::VectorXi::Constant(n6, 40));
        build_problem_Eigen(n6, mesh_, P_PrismProperty, face_handles, face_idx_2_i, B, negA_T);
        std::cout<<"build linear system takes: "<<solve_linear_system_timer.lapString()<<std::endl;

        Eigen::SimplicialCholesky<SpMat> solver(B);  // performs a Cholesky factorization  of B
        assert(solver.info() == Eigen::Success);
        Eigen::VectorXf x = solver.solve(negA_T);    // use the factorization to solve for the given right hand side

        std::cout<<"find optimal velocities takes: "<<solve_linear_system_timer.elapsedString()<<std::endl;
        //std::cout<< x << std::endl;
        // update new prisms to new position and saved in another prismProperty
        // project velocities using anthoer local minimum
        project_v_and_update_prisms(x,face_handles,lambda);
        E_k = E(face_handles);
        std::cout<<"E"<< i <<": "<<E(face_handles)<<std::endl;
        if(converge_E(E_k, E_km1)){
            std::cout<<"[Global Optimization]:converge\n";
            break;
        }
        E_km1 = E_k;
        update_vertices_based_on_prisms();
        mesh_.update_normals(); 
        glutPostRedisplay();

    }
    // update vertices position based on faces(prisms) around each vertex
    update_vertices_based_on_prisms(); 
    // update OpenMesh's normals
    mesh_.update_normals(); 
    glutPostRedisplay();
}
