#ifndef SELOOPCONSUMER_HPP
#define SELOOPCONSUMER_HPP

#include "json.hpp"
using json = nlohmann::json;

#include "cs.h"
#include "klu.h"

#include "SEConsumer.hpp"

// standard data types
#include <complex>
#include <list>
#include <unordered_map>

// SLIST holds the lists of node names and regulator names
#ifndef SLIST
#define SLIST std::list<std::string>
#endif

// SIMAP holds the one-indexed positions of nodes
#ifndef SIMAP
#define SIMAP std::unordered_map<std::string,unsigned int>
#endif

// SDMAP holds x, z, and the set of regulator taps
#ifndef SDMAP
#define SDMAP std::unordered_map<std::string,double>
#endif

// SCMAP holds the complex node voltages
#ifndef SCMAP
#define SCMAP std::unordered_map<std::string,std::complex<double>>
#endif

// SSMAP holds the mapping between sensors and nodes
#ifndef SSMAP
#define SSMAP std::unordered_map<std::string,std::string>
#endif

// this holds the voltage state
#ifndef ICMAP
#define ICMAP std::unordered_map<unisgned int,std::complex<double>>
#endif

// negligable
#define NEGL 0.00000001

// This class listens for system state messages
class SELoopConsumer : public SEConsumer {
	protected:
	string simid;

	// system state
	private:
//	cs *x, *P;		// state model
	cs *P;			// x comes from V and A but P is persistent 
	cs *F, *Q;		// process model
	cs *R;
//	cs *z, *R;		// measurement model
//	cs *h, *J;
	cs *eyex;		// identity matrix of dimension x
	int xqty;		// number of states
	int zqty;		// number of measurements

//	cs *x, *xpre, *x1, *xupd;						// state vector
//	cs *P, *Ppre, *P1, *P2, *P3, *P4, *P, *Pupd;	// state covariance
//	cs *y1, *yupd;									// residual vector
//	cs *S1, *S2, *S3, *Supd;						// residual covariance
//	cs *K1, *K2, *K3, *Kupd;						// gain matrix

	private:
	json jtext;		// object holding the input message
	json jstate;	// object holding the output message

	// system topology definition
	public:
	uint node_qty;		// number of nodes
	SLIST node_names;	// node names [list of strings]
	SIMAP node_idxs;	// node positional indices [node->int]
	SCMAP node_vnoms;	// complex nominal voltages of nodes
	IMMAP Yphys;		// Ybus [node->[row->col]] [physical units]

	// system state
	private:
	ICMAP Vpu;			// voltage state in per-unit
	IMMAP A;			// regulator tap ratios <- we need reg information
//	cs *State_Cov;		// state covariance matrix;

	private:
	const double sbase = 1000000.0;	//
	IMMAP Ypu;						//
	
	private:
	SensorArray zary;

	private:
	ofstream state_fh;	// file to record states

	private:
	SEProducer *statePublisher = NULL;

	public:
	SELoopConsumer(const string& brokerURI, 
				const string& username,
				const string& password,
				const string& target,
				const string& mode,
				const string& simid,
				const SensorArray& zary,
				const uint& node_qty,
				const SLIST& node_names,
				const SIMAP& node_idxs,
				const SCMAP& node_vnoms,
				const IMMAP& Yphys,
				const IMMAP& A) {
		for ( auto& node : node_names ) cout << node+'\n';
		this->brokerURI = brokerURI;
		this->username = username;
		this->password = password;
		this->target = target;
		this->mode = mode;
		this->simid = simid;
		this->zary = zary;
		this->node_qty = node_qty;
		this->node_names = node_names;
		this->node_idxs = node_idxs;
		this->node_vnoms = node_vnoms;
		this->Yphys = Yphys; 
		this->A = A;
		cout << "------\n";
		for ( auto& node : this->node_names ) cout << node+'\n';
		cout << "------\n";
	}

	private:
	virtual void init() {
		// set up the output message json object
		jstate["simulation_id"] = simid;

		// Construct the producer that will be used to publish the state estimate
		string sePubTopic = "goss.gridappsd.state-estimator.out."+simid;
		statePublisher = new SEProducer(brokerURI,username,password,sePubTopic,"topic");
		cout << "State Estimate Message Producer Constructed.\n";

		// --------------------------------------------------------------------
		// Establish Dimension of State Space and Measurement Space
		// --------------------------------------------------------------------
		xqty = 2*node_qty;
		zqty = zary.zqty;
		cout << "zary.zqty is " << zary.zqty << '\n';

		// --------------------------------------------------------------------
		// Initialize Voltages (complex per-unit)
		// --------------------------------------------------------------------
		for ( auto& node_name : node_names ) {
			// Important: V is indexed by node index like A and Y
//			V[node_idxs[node_name]] = node_vnoms[node_name];
			Vpu[node_idxs[node_name]] = 1.0;
		}
		cout << "Voltages Initialized.\n";

		// --------------------------------------------------------------------
		// Initialize State Covariance Matrix
		// --------------------------------------------------------------------
		double span_vmag = 1.0;
		double span_varg = 1/3*3.1415926535;
		double span_taps = 0.2;
		cs *Praw = cs_spalloc(0,0,xqty,1,1);
		for ( int idx = 0 ; idx < node_qty ; idx++ ) {
			// Add variance for the voltage magnitude state
			cs_entry(Praw,idx,idx,span_vmag);
			// Add variance for the voltage phase state
			cs_entry(Praw,node_qty+idx,node_qty+idx,span_varg);
		}
		P = cs_compress(Praw); cs_spfree(Praw);
		cout << "State Covariance Matrix Initialized.\n";


		// --------------------------------------------------------------------
		// Compute Ypu
		// --------------------------------------------------------------------
		for ( auto& inode : node_names ) {
			cout << inode << '\n';
			uint i = node_idxs[inode];
			try {
				auto& row = Yphys.at(i);
				for ( auto& jnode : node_names ) {
					uint j = node_idxs[jnode];
					try {
						complex<double> yij = row.at(j);
						complex<double> vnomi = node_vnoms[inode];
						complex<double> vnomj = node_vnoms[jnode];
						Ypu[i][j] = conj(vnomi/sbase) * yij * vnomj;
					} catch(...) {}
				}
			} catch(...) {}
		}

//		// print
//		for ( auto& inode : node_names ) {
//			uint i = node_idxs[inode];
//			try {
//				auto& row = Ypu.at(i);
//				for ( auto& jnode: node_names ) {
//					uint j = node_idxs[jnode];
//					try {
//						complex<double> yij = row.at(j);
//						complex<double> vnomi = node_vnoms[inode];
//						complex<double> vnomj = node_vnoms[jnode];
//						cout << "Y(" << i << "," << j << ") -> " << yij << '\n';
//					} catch(...) {}
//				}
//			} catch(...) {}
//		}


		// --------------------------------------------------------------------
		// Initialize cs variables for state estimation
		// --------------------------------------------------------------------
		// state transition matrix (constant)
		cs *Fraw = cs_spalloc(0,0,xqty,1,1);
		for ( int ii = 0 ; ii < xqty ; ii++ )
			cs_entry(Fraw,ii,ii,1);
		F = cs_compress(Fraw); cs_spfree(Fraw);
		print_cs_compress(F,"mat/F.csv");

		// process covariance matrix (constant)
		cs *Qraw = cs_spalloc(0,0,xqty,1,1);
		for ( int ii = 0 ; ii < xqty ; ii++ )
			cs_entry(Qraw,ii,ii,0.04*sqrt(1.0/4));		// THIS MAY NEED TO CHANGE
		Q = cs_compress(Qraw); cs_spfree(Qraw);
		print_cs_compress(Q,"mat/Q.csv");

		// identity matrix of dimension x (constant)
		cs *eyexraw = cs_spalloc(0,0,xqty,1,1);
		for ( int ii = 0 ; ii < xqty ; ii++ )
			cs_entry(eyexraw,ii,ii,1.0);
		eyex = cs_compress(eyexraw); cs_spfree(eyexraw);
		print_cs_compress(eyex,"mat/eyex.csv");

		// measurement covariance matrix (constant)
		cs *Rraw = cs_spalloc(0,0,zqty,1,1);
		int idx = 0;
		for ( auto& measurement : zary.zids ) {
			cs_entry(Rraw,idx,idx,zary.zsigs[measurement]);
			idx++;
		} R = cs_compress(Rraw); cs_spfree(Rraw);
		print_cs_compress(R,"mat/R.csv");
		// initial state vector
		
		// initial measurement vector [these actually don't need to be done here]
//		this->sample_z();
//		this->calc_h();
//		this->calc_J();


		// --------------------------------------------------------------------
		// Initialize the state recorder file
		// --------------------------------------------------------------------
		state_fh.open("vmag_per-unit.csv",ofstream::out);
		state_fh << "timestamp,";
		int ctr = 0;
		for ( auto& node_name : node_names )
			state_fh << "\'"+node_name+"\'" << ( ++ctr < node_qty ? ',' : '\n' );
		state_fh.close();

	}

	// ------------------------------------------------------------------------
	// PROCESS ()
	// ------------------------------------------------------------------------
	public:
	virtual void process() {
		cout << "\nMessage of " << text.length() 
			<< " bytes recieved on measurement topic.\n";

		jtext = json::parse(text);
		int timestamp = jtext["message"]["timestamp"];
		cout << "\ttimestamp: " << timestamp << "\n";

		for ( auto& mobj : zary.mmrids ) {
//			cout << mobj << " -> " << zary.mtypes[mobj] << '\n';
		}

		// --------------------------------------------------------------------
		// Reset the new measurement indicator
		// --------------------------------------------------------------------
		for ( auto& zid : zary.zids ) zary.znew[zid] = false;

		// --------------------------------------------------------------------
		// Use the simulation output to update the states
		// --------------------------------------------------------------------
		// This needs to translate simulation output into zary.zvals[zid]
		//  - We need to iterate over the "measurements" in the simoutput
		//  - As in SensorDefConsumer.hpp, measurements can have multiple z's


		// Next, update new measurements
		for ( auto& m : jtext["message"]["measurements"] ) {
			// link back to information about the measurement using its mRID
			string mmrid = m["measurement_mrid"];
//			cout << m.dump()+'\n';
//			cout << mmrid+'\n';
			string m_type = zary.mtypes[mmrid];
//			cout << '\t' + m_type + '\n';

			// Check for "PNV" measurement
			if ( !m_type.compare("PNV") ) {

				// update the voltage magnitude (in per-unit)
				string zid = mmrid+"_Vmag";
				double vmag_phys = m["magnitude"];
				zary.zvals[mmrid+"_Vmag"] = 
					abs ( vmag_phys / node_vnoms[zary.znode1s[zid]] );
				zary.znew[mmrid+"_Vmag"] = true;

				cout << mmrid+"_Vmag" << " -> " << zary.zvals[mmrid+"_Vmag"] << '\n';

				// update the voltage phase
				// --- LATER ---
				// -------------
			}

			// the number and type of z entries depends on the measurement type
			if ( !m_type.compare("") ) { 
			}
		}
		

		// --------------------------------------------------------------------
		// Estimate the state
		// --------------------------------------------------------------------
		cout << "ESTIMATING...\n";
		this->estimate();

// STOP EARLY
//		doneLatch.countDown();
//		return;

		// --------------------------------------------------------------------
		// Package and publish the state
		// --------------------------------------------------------------------
		
		// Initializae json
		json jstate;
		jstate["message"] = json::object();
		jstate["message"]["timestamp"] = timestamp;
		jstate["message"]["measurements"] = json::array();

		for ( auto& node_name : node_names ) {
			// build a json object for each node
			// NOTE: I will not spend too much time on this until we know
			//	exactly what the message should look like
			//  - best guess is node and phase as commented out below
			//  - this would require a new sparql query and data flows
			json node_state;
//			node_state["ConnectivityNode"] = node_buses[node_name];
//			node_state["phases"] = node_phases[node_name];
			node_state["dss_node"] = node_name;
//			node_state["measurementClass"] = "Analog";
			node_state["measurementType"] = "PNV";
			// add the state values
			uint idx = node_idxs[node_name];
			complex<double> vnom = node_vnoms[node_name];
			node_state["magnitude"] = abs ( vnom * Vpu[idx] );
			node_state["angle"] = 180.0/3.1415926535 * arg ( vnom * Vpu[idx] );
			// append this state to the measurement array
			jstate["message"]["measurements"].push_back(node_state);
		}

//		for ( auto& reg_name : reg_names ) {}

		// Publish the message
		cout << jstate.dump(4).substr(200)+'\n';;
		statePublisher->send(jstate.dump());

		// write to file
		state_fh.open("vmag_per-unit.csv",ofstream::app);
		state_fh << timestamp << ',';
		int ctr = 0;
		for ( auto& node_name : node_names ) {
			double vmag_pu = abs( Vpu[ node_idxs[node_name] ] );
			state_fh << vmag_pu << ( ++ctr < node_qty ? ',' : '\n' );
		}
		state_fh.close();


		// --------------------------------------------------------------------
		// Check whether to stop
		// --------------------------------------------------------------------
		if ( text == "stop" ) {
			cout << "TIME TO STOP!\n";
			statePublisher->close();
			doneLatch.countDown();
		}
	}

	private:
	void estimate(void) {
		cout << "xqty is " << xqty << '\n';
		cout << "zqty is " << zqty << '\n';
		cout << "F is " << F->m << " by " << F->n << " with " << F->nzmax << " entries\n";
		cout << "Q is " << Q->m << " by " << Q->n << " with " << Q->nzmax << " entries\n";


		// WE NEED TO HANDLE R-MASK IN HERE SOMEWHERE; ZARY NEEDS TO BE PERSISTANT

		// x, z, h, and J will be maintained here
		
		cout << "prepx ... ";
		cs *x; this->prep_x(x);
		cout << "x is " << x->m << " by " << x->n << 
			" with " << x->nzmax << " entries\n";
		print_cs_compress(x,"mat/x.csv");
		cout << "P is " << P->m << " by " << P->n << 
			" with " << P->nzmax << " entries\n";
		print_cs_compress(P,"mat/P.csv");
	
		cout << "sample_z ... ";
		cs *z; this->sample_z(z);
		cout << "z is " << z->m << " by " << z->n << 
			" with " << z->nzmax << " entries\n";
		print_cs_compress(z,"mat/z.csv");

		cout << "calc_h ... ";
		cs *h; this->calc_h(h);
		cout << "h is " << h->m << " by " << h->n << 
			" with " << h->nzmax << " entries\n";
//		for ( int idx = 0 ; idx < h->nzmax ; idx++ )
//			cout << "\th[" << h->i[idx] << "] is " << h->x[idx] << '\n';
		print_cs_compress(h,"mat/h.csv");

		cout << "calcJ ... ";
		cs *J; this->calc_J(J);
		cout << "J is " << J->m << " by " << J->n << 
			" with " << J->nzmax << " entries\n";
		print_cs_compress(J,"mat/J.csv");

		// --------------------------------------------------------------------
		// Predict Step
		// --------------------------------------------------------------------
		// -- compute x_predict = F*x | F=I (to improve performance, skip this)
		cs *xpre = cs_multiply(F,x);
		if (!xpre) cout << "ERROR: null xpre\n";
		else cout << "xpre is " << xpre->m << " by " << xpre->n << 
			" with " << xpre->nzmax << " entries\n";

		// -- compute p_predict = F*P*F' + Q | F=I (can be simplified)

		cs *P1 = cs_transpose(F,1);
		if (!P1) cout << "ERROR: null P1\n";
		else cout << "P1 is " << P1->m << " by " << P1->n << 
			" with " << P1->nzmax << " entries\n";

		cs *P2 = cs_multiply(P,P1); cs_spfree(P1);
		if (!P2) cout << "ERROR: null P2\n";
		else cout << "P2 is " << P2->m << " by " << P2->n << 
			" with " << P2->nzmax << " entries\n";

		cs *P3 = cs_multiply(F,P2); cs_spfree(P2);
		if (!P3) cout << "ERROR: null P3\n";
		else cout << "P3 is " << P3->m << " by " << P3->n << 
			" with " << P3->nzmax << " entries\n";

		cs *Ppre = cs_add(P3,Q,1,1); cs_spfree(P3);
		if (!Ppre) cout << "ERROR: null Ppre\n";
		else cout << "Ppre is " << Ppre->m << " by " << Ppre->n << 
			" with " << Ppre->nzmax << " entries\n";

		cout << "Predict step complete.\n";
		
		// --------------------------------------------------------------------
		// Update Step
		// --------------------------------------------------------------------
		// -- compute y = J*x_predict + z

		cs *y1 = cs_multiply(J,xpre);
		if (!y1) cout << "ERROR: null y1\n";
		else cout << "y1 is " << y1->m << " by " << y1->n << 
			" with " << y1->nzmax << " entries\n";

		cs *yupd = cs_add(z,y1,1,-1); cs_spfree(y1);
		if (!yupd) cout << "ERROR: null yupd\n";
		else cout << "yupd is " << yupd->m << " by " << yupd->n << 
			" with " << yupd->nzmax << " entries\n";

		cout << "y updated\n";

		// -- compute S = J*P_predict*J' + R

		cs *S1 = cs_transpose(J,1);
		if (!S1) cout << "ERROR: null S1\n";
		else cout << "S1 is " << S1->m << " by " << S1->n << 
			" with " << S1->nzmax << " entries\n";
		print_cs_compress(S1,"mat/S1.csv");

		cs *S2 = cs_multiply(Ppre,S1); cs_spfree(S1);
		if (!S2) cout << "ERROR: null S2\n";
		else cout << "S2 is " << S2->m << " by " << S2->n << 
			" with " << S2->nzmax << " entries\n";
		print_cs_compress(S2,"mat/S2.csv");

		cs *S3 = cs_multiply(J,S2); cs_spfree(S2);
		if (!S3) cout << "ERROR: null S3\n";
		else cout << "S3 is " << S3->m << " by " << S3->n << 
			" with " << S3->nzmax << " entries\n";
		print_cs_compress(S3,"mat/S3.csv");

		cs *Supd = cs_add(R,S3,1,1); cs_spfree(S3);
		if (!Supd) cout << "ERROR: null Supd\n";
		else cout << "Supd is " << Supd->m << " by " << Supd->n << 
			" with " << Supd->nzmax << " entries\n";
		print_cs_compress(Supd,"mat/Supd.csv");

		cout << "S updated\n";
		cout << "Supd is " << Supd->m << " by " << Supd->n << 
			" with " << Supd->nzmax << " entries\n";
//		cout << "Supd->nzmax is: " << Supd->nzmax << '\n';
//		cout << "Supd->p is: " << Supd->p << '\n';
//		for ( int ii = 0 ; ii < Supd->m + 1 ; ii++ )
//			cout << '\t' << Supd->p[ii] << '\n';
//		cout << "Supd->i is: " << Supd->i << '\n';
//		for ( int ii = 0 ; ii < Supd->nzmax ; ii++ )
//			cout << '\t' << Supd->i[ii] << ", ";
//		cout << '\n';
//		for ( int ii = 0 ; ii < Supd->nzmax ; ii++ )
//			cout << '\t' << Supd->x[ii] << ", ";
//		cout << '\n';

		// -- compute K = P_predict*J'*S^-1
		cs *K1 = cs_transpose(J,1);
		if (!K1) cout << "ERROR: null K1\n";
		else cout << "K1 is " << K1->m << " by " << K1->n << 
			" with " << K1->nzmax << " entries\n";

		cs *K2 = cs_multiply(Ppre,K1); cs_spfree(K1);
		if (!K2) cout << "ERROR: null K2\n";
		else cout << "K2 is " << K2->m << " by " 
			<< K2->n << " with " << K2->nzmax << " entries\n";

		cs *K3raw = cs_spalloc(0,0,zqty*zqty,1,1);
		if (!K3raw) cout << "ERROR: null K3raw\n";
		else cout << "K3raw is " << K3raw->m << " by " 
			<< K3raw->n << " with " << K3raw->nzmax << " entries\n";

		try {
		
			cout << "in KLU block\n";

			// Initialize klusolve variables
			klu_symbolic *klusym;
			klu_numeric *klunum;
			klu_common klucom;
			if (!klu_defaults(&klucom)) throw "klu_defaults failed";

			cout << "klucom initialized.\n";
			
			cout << Supd->m << '\t' << Supd->p << '\t' << Supd->i << '\n';
			klusym = klu_analyze(Supd->m,Supd->p,Supd->i,&klucom);
			if (!klusym) throw "klu_analyze failed";

			cout << "klusym initialized.\n";

			klunum = klu_factor(Supd->p,Supd->i,Supd->x,klusym,&klucom);
			if (!klunum) {
				cout << "Common->status is: " << klucom.status << '\n';
				if ( klucom.status == 1 ) cout << "\tKLU_SINGULAR\n";
				throw "klu_factor failed";
			}

			cout << "klunum initialized.\n";

			// initialize an identiy right-hand side
			double *rhs = new double[zqty*zqty];
			for ( int ii = 0 ; ii < zqty*zqty ; ii++ )
				rhs[ii] = ii/zqty == ii%zqty ? 1 : 0;
			
			cout << "identity rhs created\n";

			klu_solve(klusym,klunum,Supd->m,Supd->n,rhs,&klucom);
			if (klucom.status) {
				cout << "Common->status is: " << klucom.status << '\n';
				throw "klu_solve failed";
			}

			cout << "klu_solve complete\n";

			// convert the result to cs*
			for ( int ii = 0 ; ii < zqty ; ii++ )
				for ( int jj = 0 ; jj < zqty ; jj++ )
					if (rhs[ii+zqty*jj])
						cs_entry(K3raw,ii,jj,rhs[ii+zqty*jj]);

			cout << "rhs coppied to K3raw\n";

			delete rhs;
		} catch (const char *msg) {
			cout << "ERROR: " << msg << '\n';
			return;
		}

		cout << "left KLU block\n";

		cs *K3 = cs_compress(K3raw); cs_spfree(K3raw);
		if ( !K3 ) cout << "ERROR: K3 null\n";
		else cout << "K3 is " << K3->m << " by " << K3->n << " with " << K3->nzmax << " entries\n";

		cs *Kupd = cs_multiply(K2,K3); cs_spfree(K2); cs_spfree(K3);
		if ( !Kupd ) cout << "ERROR: Kupd null\n";
		else cout << "Kupd is " << Kupd->m << " by " << Kupd->n << " with " << Kupd->nzmax << " entries\n";

		cout << "K updated\n";

		// -- compute x_update = x_predict + K * y

		cs *x1 = cs_multiply(Kupd,yupd);
		if ( !x1 ) cout << "ERROR: x1 null\n";
		else cout << "x1 is " << x1->m << " by " << x1->n << " with " << x1->nzmax << " entries\n";
		
		cs *xupd = cs_add(xpre,x1,1,1); cs_spfree(x1);
		if ( !xupd ) cout << "ERROR: xupd null\n";
		else cout << "xupd is " << xupd->m << " by " << xupd->n << " with " << xupd->nzmax << " entries\n";

		cout << "x updated\n";

		// -- compute P_update = (KYH+I)*P_predict
		cs *P4 = cs_multiply(Kupd,J);
		if ( !P4 ) cout << "ERROR: P4 null\n";
		else cout << "P4 is " << P4->m << " by " << P4->n << " with " << P4->nzmax << " entries\n";

		cs *P5 = cs_add(eyex,P4,1,-1); cs_spfree(P4);
		if ( !P5 ) cout << "ERROR: P5 null\n";
		else cout << "P5 is " << P5->m << " by " << P5->n << " with " << P5->nzmax << " entries\n";

		cs *Pupd = cs_multiply(P5,Ppre); cs_spfree(P5);
		if ( !Pupd ) cout << "ERROR: Pupd null\n";
		else cout << "Pupd is " << Pupd->m << " by " << Pupd->n << " with " << P->nzmax << " entries\n";

		cout << "P updated\n";

		cout << "Update step complete.\n";

		// --------------------------------------------------------------------
		// Update persistant state (Vpu and A)
		// --------------------------------------------------------------------
		cout << "calling decompress_state_xupd\n";
		if (xupd) decompress_state(xupd);

		// --------------------------------------------------------------------
		// Clean up
		// --------------------------------------------------------------------
		// update system state
		cout << "freeing xpre and Ppre...\n";
		cs_spfree(xpre); cs_spfree(Ppre);

		// X HAS BEEN DECOMPRESSED INTO VPU AND A
//		cout << "freeing x and setting xupd to NULL...\n";
//		cs_spfree(x); x = xupd; xupd = NULL;
		cout << "freeing P and setting Pupd to NULL...\n";
		cs_spfree(P); P = Pupd; Pupd = NULL;
		// free residual
		cout << "freeing ypud and Supd...\n";
		cs_spfree(yupd); cs_spfree(Supd);
		// free gain matrix
		cout << "freeing Kupd...\n";
		cs_spfree(Kupd);
		// free measurement variableas
//		cout << "freeing x, P, z, h, and J...\n";
//		cs_spfree(x); cs_spfree(P); cs_spfree(z); cs_spfree(h); cs_spfree(J);
		cout << "freeing x, z, h, and J\n";
		cs_spfree(x); cs_spfree(z); cs_spfree(h); cs_spfree(J);

		// Need to handle P
//		// - P points to State_Cov -- we need the former to 
//		cout << "Updating State_Cov and stuff...\n";
//		cs *tmp = State_Cov;
//		State_Cov = P; P = NULL;
//		cs_spfree(tmp);


	}

	private:
	void decompress_state(cs *&x) {
		// copy state into vector (states, especially phase, can be 0)
		vector<double> xvec(x->m,0.0);
		for ( int idx = 0 ; idx < x->nzmax ; idx++ )
			xvec[x->i[idx]] = x->x[idx];
		// update Vpu
		//  - NOTE: THIS WILL CHANGE IF SOURCE BUS STATES ARE NOT IN X
		for ( auto& node_name : node_names ) {
			uint idx = node_idxs[node_name];
			uint vidx = idx-1;
			uint Tidx = node_qty + vidx - 1;
			double vrei = xvec[vidx] * cos(xvec[Tidx]);
			double vimi = xvec[vidx] * sin(xvec[Tidx]);
			Vpu[idx] = complex<double>(vrei,vimi);
		}
		// update A
//		for ( auto& reg_name : SLIST reg_names ) {}
	}

	private:
	void prep_x(cs *&x) {
		// Prepare x
		cs *xraw = cs_spalloc(xqty,1,xqty,1,1);
		for ( auto& node_name : node_names ) {
			// Find the per-unit voltage of active node
			uint idx = node_idxs[node_name];
			complex<double> Vi = Vpu[idx];
			// Add the voltage magnitude to x
			uint vidx = idx - 1;
			if ( abs(Vi) > NEGL ) cs_entry(xraw,vidx,0,abs(Vi));
			// Add the voltage angle to x
			uint Tidx = node_qty + vidx - 1;
			if ( arg(Vi) ) cs_entry(xraw,Tidx,0,arg(Vi));
		}
		x = cs_compress(xraw); cs_spfree(xraw);
	}

	private:
	void sample_z(cs *&z) {
		// measurements have been loaded from the sim output message to zary
		cs *zraw = cs_spalloc(zqty,1,zqty,1,1);
		if ( !zraw ) cout << "Failed to cs_spalloc zraw\n";
		for ( auto& zid : zary.zids )
			if ( zary.zvals[zid] > NEGL )
				cs_entry(zraw,zary.zidxs[zid],0,zary.zvals[zid]);
		z = cs_compress(zraw); 
		cs_spfree(zraw);
	}

	private:
	double vi;
	double vj;
	double T;
	double g;
	double b;
	double ai;
	double aj;


	private:
	void set_n(uint i, uint j) {
		if ( !i ) {
			cout << "ERROR: Unexpected call to set_n with i=0\n";
			return;
		}
		if ( !j ) {
			// from node i to the reference node
			vi = abs(Vpu[i]);
			vj = 0;
			T = arg(Vpu[i]);
			ai = 0;
			aj = 0;
			// for per-unit Ybus, shunt admittance is the sum of row admittances
			complex<double> Yi0;
			try {
				auto& Yrow = Ypu.at(i);
				for ( auto& yij_pair : Yrow )
					Yi0 += yij_pair.second;
			} catch(...) {}
			g = real(Yi0);
			b = imag(Yi0);
		}
		else {
			vi = abs(Vpu[i]);
			vj = abs(Vpu[j]);
			T = arg(Vpu[i]) - arg(Vpu[j]);
			// make sure not to reduce sparcity of Y; if Y exists, we can try A
			complex<double> Yij;
			ai = 0;
			aj = 0;
			try {
				auto& Yrow = Ypu.at(i);
				try {
					Yij = Yrow.at(j);
					// We know the nodes are coupled; check for Aij
					// NOTE: A is never iterated over so we don't necessarily need at()
					ai = 1;
					try {
						auto Arow = A.at(i);
						try {
							ai = real(Arow.at(j));
						} catch(...) {}
					} catch(...) {}
					// We know the nodes are coupled; check for Aji
					// NOTE: A is never iterated over so we don't necessarily need at()
					aj = 1;
					try {
						auto Arow = A.at(j);
						try {
							aj = real(Arow.at(i));
						} catch (...) {}
					} catch(...) {}
				} catch(...) {}
			} catch(...) {}
			g = real(-1.0*Yij);
			b = imag(-1.0*Yij);
		}
	}
	

	private:
	void calc_h(cs *&h) {
		// each z component has a measurement function component
		cs *hraw = cs_spalloc(zqty,1,zqty,1,1);
		for ( auto& zid : zary.zids ) {
			cout << zid << '\n';
			uint zidx = zary.zidxs[zid];
			string ztype = zary.ztypes[zid];
			// Determine the type of z component
			if ( !ztype.compare("Pi") ) {
				// Real power injection into node i
				uint i = node_idxs[zary.znode1s[zid]];
				double Pi = 0;
				try {
					auto& Yrow = Ypu.at(i);
					for ( auto& rowpair : Yrow ) {
						uint j = rowpair.first;
						set_n(i,j);
						// Add the real power component flowing from i to j
						Pi = Pi + vi*vi/ai/ai * g - 
							vi*vj/ai/aj * (g*cos(T) + b*sin(T));
					}
					// Add the real power component flowing from i to 0
					set_n(i,0);
					Pi += vi*vi * g;
				} catch(...) {}
				// Insert the measurement component
				if ( abs(Pi) > NEGL ) cs_entry(hraw,zidx,0,Pi);
			}
			else if ( !zary.ztypes[zid].compare("Qi") ) {
				// Reactive power injection into node i
				uint i = node_idxs[zary.znode1s[zid]];
				double Qi = 0;
				try {
					auto& Yrow = Ypu.at(i);
					for ( auto& rowpair : Yrow ) {
						uint j = rowpair.first;
						set_n(i,j);
						// Add the reactive power component flowing from i to j
						Qi = Qi - vi*vi/ai/ai * b - 
							vi*vj/ai/aj * (g*sin(T) - b*cos(T));
					}
					// Add the reactive power component flowing from i to 0
					set_n(i,0);
					Qi -= vi*vi * b;
				} catch(...) {}
				if ( abs(Qi) > NEGL ) cs_entry(hraw,zidx,0,Qi);
			}
			else if ( !zary.ztypes[zid].compare("aji" ) ) {
				// aji is a direct state measurement
			}
			else if ( !zary.ztypes[zid].compare("Vmag") ) {
				// Vmag is a direct state measurement
				uint i = node_idxs[zary.znode1s[zid]];
				if ( abs(Vpu[i]) > NEGL ) cs_entry(hraw,zidx,0,abs(Vpu[i]));
			}
			else { 
				cout << "WARNING: Undefined measurement type " + ztype + '\n';
			}
		}
		h = cs_compress(hraw); cs_spfree(hraw);

	}

	private:
	void calc_J(cs *&J) {
		// each z component has a Jacobian component for each state
		cs *Jraw = cs_spalloc(zqty,xqty,zqty*xqty,1,1);
		// loop over z
		for ( auto& zid : zary.zids ) {
			uint zidx = zary.zidxs[zid];
			string ztype = zary.ztypes[zid];
			uint i = node_idxs[zary.znode1s[zid]];

			// loop over voltage magnitude states
			for ( auto& node_name : node_names ) {
				uint vidx = node_idxs[node_name];
				uint xidx = vidx-1;
				// Computation of d/dv depends on the measurement type
				if ( !ztype.compare("Pi" ) ) {
					if ( vidx == i ) {
						// --- compute dPi/dvi
						double dP = 0;
						// loop over adjacent nodes
						try {
							auto& Yrow = Ypu.at(i);
							for ( auto& rowpair : Yrow ) {
								uint j = rowpair.first;
								set_n(i,j);
								dP = dP + 2*vi/ai/ai * g - 
									vj/ai/aj * (g*cos(T) + b*sin(T));
							}
							// consider the reference node
							set_n(i,0);
							dP = dP + 2*vi * g;
						} catch(...) {}
						if ( abs(dP > NEGL ) ) cs_entry(Jraw,zidx,xidx,dP);
					} else {
						// --- compute dPi/dvj
						uint j = vidx;
						set_n(i,j);
						double dP = -1.0 * vi/ai/aj * (g*cos(T) + b*sin(T));
						if ( abs(dP) > NEGL ) cs_entry(Jraw,zidx,xidx,dP);
					}
				}
				else if ( !ztype.compare("Qi") ) {
					if ( vidx == i ) {
						// --- compute dQ/dvi
						double dQ = 0;
						// loop over adjacent nodes
						try {
							auto& Yrow = Ypu.at(i);
							for ( auto& rowpair : Yrow ) {
								uint j = rowpair.first;
								set_n(i,j);
								dQ = dQ - 2*vi/ai/ai * b - 
									vj/ai/aj * (g*sin(T) - b*cos(T));
							}
							// consider the reference node
							set_n(i,0);
							dQ = dQ - 2*vi*b;
						} catch (...) {}
						if ( abs(dQ) > NEGL ) cs_entry(Jraw,zidx,xidx,dQ);
					} else {
						// --- compute dQ/dvj
						uint j = vidx;
						set_n(i,j);
						double dQ = -1.0 * vi/ai/aj * (g*sin(T) - b*cos(T));
						if ( abs(dQ) > NEGL ) cs_entry(Jraw,zidx,xidx,dQ);
					}
				}
				else if ( !ztype.compare("aji") ) {
					// daji/dv = 0
				}
				else if ( !ztype.compare("Vmag") ) {
					if ( vidx == i ) {
						// --- compute dvi/dvi
						cs_entry(Jraw,zidx,xidx,1.0);
					}
				}
				else {
					cout << "WARNING: Undefined measurement type " + ztype + '\n';
				}
			}

			// loop over voltage phase states
			for ( auto& node_name : node_names ) {
				// IF ( NOT SOURCE NODE ) ???
				uint vidx = node_idxs[node_name];
				uint xidx = vidx-1 + node_qty; // THIS WOULD CHANGE IF SOURCES KNOWN
				if ( !ztype.compare("Pi") ) {
					if ( vidx == i ) {
						// --- compute dPi/dTi
						double dP = 0;
						// loop over adjacent nodes
						try {
							auto &Yrow = Ypu.at(i);
							for ( auto& rowpair : Yrow ) {
								uint j = rowpair.first;
								set_n(i,j);
								dP = dP + vi*vj/ai/aj * (g*sin(T) - b*cos(T));
							}
							// reference node component is 0
						} catch(...) {}
						if ( abs(dP) > NEGL ) cs_entry(Jraw,zidx,xidx,dP);
					} else {
						// --- compute dP/dTj
						uint j = vidx;
						set_n(i,j);
						double dP = -1.0 * vi*vj/ai/aj * (g*sin(T) - b*cos(T));
						if ( abs(dP) > NEGL ) cs_entry(Jraw,zidx,xidx,dP);
					}
				}
				else if ( !ztype.compare("Qi") ) {
					if ( vidx == i ) {
						// compute dQi/dTi
						double dQ = 0;
						// loop over adjacent nodes
						try {
							auto& Yrow = Ypu.at(i);
							for ( auto& rowpair : Yrow ) {
								uint j = rowpair.first;
								set_n(i,j);
								dQ = dQ - vi*vj/ai/aj * (g*cos(T) + b*sin(T));
							}
							// reference component is 0
						} catch(...) {}
						if (abs(dQ) > NEGL ) cs_entry(Jraw,zidx,xidx,dQ);
					} else {
						// --- compute dQ/dTj
						uint j = vidx;
						set_n(i,j);
						double dQ = vi*vj/ai/aj * (g*cos(T) + b*sin(T));
						if ( abs(dQ) > NEGL ) cs_entry(Jraw,zidx,xidx,dQ);
					}
				}
				else if ( !ztype.compare("ajj") ) {
					// dajj/dT = 0
				}
				else if ( !ztype.compare("Vmag") ) {
					// dvi/dT = 0
				}
				else {
					cout << "WARNING: Undefined measurement type " + ztype + '\n';
				}
			}

			// loop over regulator tap ratio states
			// --- LATER ---
			// -------------


		}
		J = cs_compress(Jraw); cs_spfree(Jraw);
	}
	
	private:
	void print_cs_compress(cs *&a, const string &filename="cs.csv") {
		// First copy into a map
		unordered_map<int,unordered_map<int,double>> mat;
		for ( int i = 0 ; i < a->n ; i++ ) {
//			cout << "in column " << i << " idx from " << a->p[i] << " to " << a->p[i+1] << '\n';
			for ( int j = a->p[i] ; j < a->p[i+1] ; j++ ) {
				mat[a->i[j]][i] = a->x[j];
			}
		}
		// write to file
		ofstream ofh;
		ofh.open(filename,ofstream::out);
		cout << "writing " + filename + "\n\n";	
		for ( int i = 0 ; i < a->m ; i++ )
			for ( int j = 0 ; j < a->n ; j++ )
				ofh << mat[i][j] << ( j == a->n-1 ? '\n' : ',' );
		ofh.close();
	}
};




#endif
