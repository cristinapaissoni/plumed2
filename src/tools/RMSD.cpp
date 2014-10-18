/* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
   Copyright (c) 2011-2014 The plumed team
   (see the PEOPLE file at the root of the distribution for a list of names)

   See http://www.plumed-code.org for more information.

   This file is part of plumed, version 2.

   plumed is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   plumed is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with plumed.  If not, see <http://www.gnu.org/licenses/>.
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ */
#include "RMSD.h"
#include "PDB.h"
#include "Log.h"
#include "Exception.h"
#include <cmath>
#include <iostream>
#include "Tools.h"
using namespace std;
namespace PLMD{

RMSD::RMSD() : alignmentMethod(SIMPLE),reference_center_is_calculated(false),positions_center_is_calculated(false) {}

///
/// general method to set all the rmsd property at once by using a pdb where occupancy column sets the weights for the atoms involved in the 
/// alignment and beta sets the weight that are used for calculating the displacement. 
///
void RMSD::set(const PDB&pdb, string mytype ){

	setReference(pdb.getPositions());
	setAlign(pdb.getOccupancy());
	setDisplace(pdb.getBeta());
        setType(mytype);

}

void RMSD::setType(string mytype){

	alignmentMethod=SIMPLE; // initialize with the simplest case: no rotation
	if (mytype=="SIMPLE"){
		alignmentMethod=SIMPLE;
	}
	else if (mytype=="OPTIMAL"){
		alignmentMethod=OPTIMAL;
	}
	else if (mytype=="OPTIMAL-FAST"){
		alignmentMethod=OPTIMAL_FAST;
	}
	else plumed_merror("unknown RMSD type" + mytype);

}

void RMSD::clear(){
  reference.clear();
  reference_center_is_calculated=false;
  reference_center_is_removed=false;
  align.clear();
  displace.clear();
  positions_center_is_calculated=false;
  positions_center_is_removed=false;
}

string RMSD::getMethod(){
	string mystring;
	switch(alignmentMethod){
		case SIMPLE: mystring.assign("SIMPLE");break; 
		case OPTIMAL: mystring.assign("OPTIMAL");break; 
		case OPTIMAL_FAST: mystring.assign("OPTIMAL-FAST");break; 
	}	
	return mystring;
}
///
/// this calculates the center of mass for the reference and removes it from the reference itself
/// considering uniform weights for alignment
///
void RMSD::setReference(const vector<Vector> & reference){
  unsigned n=reference.size();
  this->reference=reference;
  plumed_massert(align.empty(),"you should first clear() an RMSD object, then set a new reference");
  plumed_massert(displace.empty(),"you should first clear() an RMSD object, then set a new reference");
  align.resize(n,1.0/n);
  displace.resize(n,1.0/n);
  for(unsigned i=0;i<n;i++) reference_center+=this->reference[i]*align[i];
  for(unsigned i=0;i<n;i++) this->reference[i]-=reference_center;
  reference_center_is_calculated=true;
  reference_center_is_removed=true;
}
///
/// the alignment weights are here normalized to 1 and  the center of the reference is removed accordingly
///
void RMSD::setAlign(const vector<double> & align, bool normalize, bool remove_center){
  unsigned n=reference.size();
  plumed_massert(this->align.size()==align.size(),"mismatch in dimension of align/displace arrays");
  this->align=align;
  double w=0.0;
  for(unsigned i=0;i<n;i++) w+=this->align[i];
  double inv=1.0/w;
  if(normalize){for(unsigned i=0;i<n;i++) this->align[i]*=inv;}
  // just remove the center if that is asked
  if(remove_center){
  	   // if the center was removed before, then add it and store the new one
	  if(reference_center_is_removed){
		plumed_massert(reference_center_is_calculated," seems that the reference center has been removed but not calculated and stored!");	
		addCenter(reference,reference_center);
	  }
	  reference_center=calculateCenter(reference,this->align);
	  removeCenter(reference,reference_center);
	  reference_center_is_calculated=true;
	  reference_center_is_removed=true;
  }
}
///
/// here the weigth for normalized weighths are normalized and set
///
void RMSD::setDisplace(const vector<double> & displace, bool normalize){
  unsigned n=reference.size();
  plumed_massert(this->displace.size()==displace.size(),"mismatch in dimension of align/displace arrays");
  this->displace=displace;
  double w=0.0;
  for(unsigned i=0;i<n;i++) w+=this->displace[i];
  double inv=1.0/w;
  if(normalize){for(unsigned i=0;i<n;i++) this->displace[i]*=inv;}
}
///
/// This is the main workhorse for rmsd that decides to use specific optimal alignment versions
///
double RMSD::calculate(const std::vector<Vector> & positions,std::vector<Vector> &derivatives, bool squared)const{

  double ret=0.;

  switch(alignmentMethod){
	case SIMPLE:
		//	do a simple alignment without rotation 
		ret=simpleAlignment(align,displace,positions,reference,derivatives,squared);
		break;	
	case OPTIMAL_FAST:
		// this is calling the fastest option:
                if(align==displace) ret=optimalAlignment<false,true>(align,displace,positions,reference,derivatives,squared); 
                else                ret=optimalAlignment<false,false>(align,displace,positions,reference,derivatives,squared); 
		break;
	case OPTIMAL:
		// this is the fast routine but in the "safe" mode, which gives less numerical error:
		if(align==displace) ret=optimalAlignment<true,true>(align,displace,positions,reference,derivatives,squared); 
		else ret=optimalAlignment<true,false>(align,displace,positions,reference,derivatives,squared); 
		break;	
  }	

  return ret;

}

double RMSD::simpleAlignment(const  std::vector<double>  & align,
		                     const  std::vector<double>  & displace,
		                     const std::vector<Vector> & positions,
		                     const std::vector<Vector> & reference ,
		                     std::vector<Vector>  & derivatives, bool squared)const{
      double dist(0);
      unsigned n=reference.size();

      Vector apositions;
      Vector areference;
      Vector dpositions;
      Vector dreference;

      for(unsigned i=0;i<n;i++){
        double aw=align[i];
        double dw=displace[i];
        apositions+=positions[i]*aw;
        areference+=reference[i]*aw;
        dpositions+=positions[i]*dw;
        dreference+=reference[i]*dw;
      }

      Vector shift=((apositions-areference)-(dpositions-dreference));
      for(unsigned i=0;i<n;i++){
        Vector d=(positions[i]-apositions)-(reference[i]-areference);
        dist+=displace[i]*d.modulo2();
        derivatives[i]=2*(displace[i]*d+align[i]*shift);
      }

     if(!squared){
	// sqrt
        dist=sqrt(dist);
	///// sqrt on derivatives
        for(unsigned i=0;i<n;i++){derivatives[i]*=(0.5/dist);}
      }
      return dist;
}

#ifdef OLDRMSD
// notice that in the current implementation the safe argument only makes sense for
// align==displace
template <bool safe,bool alEqDis>
double RMSD::optimalAlignment(const  std::vector<double>  & align,
                                     const  std::vector<double>  & displace,
                                     const std::vector<Vector> & positions,
                                     const std::vector<Vector> & reference ,
                                     std::vector<Vector>  & derivatives, bool squared)const{
  double dist(0);
  const unsigned n=reference.size();
// This is the trace of positions*positions + reference*reference
  double rr00(0);
  double rr11(0);
// This is positions*reference
  Tensor rr01;

  derivatives.resize(n);

  Vector cpositions;

// first expensive loop: compute centers
  for(unsigned iat=0;iat<n;iat++){
    double w=align[iat];
    cpositions+=positions[iat]*w;
  }

// second expensive loop: compute second moments wrt centers
  for(unsigned iat=0;iat<n;iat++){
    double w=align[iat];
    rr00+=dotProduct(positions[iat]-cpositions,positions[iat]-cpositions)*w;
    rr11+=dotProduct(reference[iat],reference[iat])*w;
    rr01+=Tensor(positions[iat]-cpositions,reference[iat])*w;
  }

  Matrix<double> m=Matrix<double>(4,4);
  m[0][0]=2.0*(-rr01[0][0]-rr01[1][1]-rr01[2][2]);
  m[1][1]=2.0*(-rr01[0][0]+rr01[1][1]+rr01[2][2]);
  m[2][2]=2.0*(+rr01[0][0]-rr01[1][1]+rr01[2][2]);
  m[3][3]=2.0*(+rr01[0][0]+rr01[1][1]-rr01[2][2]);
  m[0][1]=2.0*(-rr01[1][2]+rr01[2][1]);
  m[0][2]=2.0*(+rr01[0][2]-rr01[2][0]);
  m[0][3]=2.0*(-rr01[0][1]+rr01[1][0]);
  m[1][2]=2.0*(-rr01[0][1]-rr01[1][0]);
  m[1][3]=2.0*(-rr01[0][2]-rr01[2][0]);
  m[2][3]=2.0*(-rr01[1][2]-rr01[2][1]);
  m[1][0] = m[0][1];
  m[2][0] = m[0][2];
  m[2][1] = m[1][2];
  m[3][0] = m[0][3];
  m[3][1] = m[1][3];
  m[3][2] = m[2][3];

  Tensor dm_drr01[4][4];
  if(!alEqDis){
    dm_drr01[0][0] = 2.0*Tensor(-1.0, 0.0, 0.0,  0.0,-1.0, 0.0,  0.0, 0.0,-1.0);
    dm_drr01[1][1] = 2.0*Tensor(-1.0, 0.0, 0.0,  0.0,+1.0, 0.0,  0.0, 0.0,+1.0);
    dm_drr01[2][2] = 2.0*Tensor(+1.0, 0.0, 0.0,  0.0,-1.0, 0.0,  0.0, 0.0,+1.0);
    dm_drr01[3][3] = 2.0*Tensor(+1.0, 0.0, 0.0,  0.0,+1.0, 0.0,  0.0, 0.0,-1.0);
    dm_drr01[0][1] = 2.0*Tensor( 0.0, 0.0, 0.0,  0.0, 0.0,-1.0,  0.0,+1.0, 0.0);
    dm_drr01[0][2] = 2.0*Tensor( 0.0, 0.0,+1.0,  0.0, 0.0, 0.0, -1.0, 0.0, 0.0);
    dm_drr01[0][3] = 2.0*Tensor( 0.0,-1.0, 0.0, +1.0, 0.0, 0.0,  0.0, 0.0, 0.0);
    dm_drr01[1][2] = 2.0*Tensor( 0.0,-1.0, 0.0, -1.0, 0.0, 0.0,  0.0, 0.0, 0.0);
    dm_drr01[1][3] = 2.0*Tensor( 0.0, 0.0,-1.0,  0.0, 0.0, 0.0, -1.0, 0.0, 0.0);
    dm_drr01[2][3] = 2.0*Tensor( 0.0, 0.0, 0.0,  0.0, 0.0,-1.0,  0.0,-1.0, 0.0);
    dm_drr01[1][0] = dm_drr01[0][1];
    dm_drr01[2][0] = dm_drr01[0][2];
    dm_drr01[2][1] = dm_drr01[1][2];
    dm_drr01[3][0] = dm_drr01[0][3];
    dm_drr01[3][1] = dm_drr01[1][3];
    dm_drr01[3][2] = dm_drr01[2][3];
  }

  vector<double> eigenvals;
  Matrix<double> eigenvecs;
  int diagerror=diagMat(m, eigenvals, eigenvecs );

  if (diagerror!=0){
    string sdiagerror;
    Tools::convert(diagerror,sdiagerror);
    string msg="DIAGONALIZATION FAILED WITH ERROR CODE "+sdiagerror;
    plumed_merror(msg);
  }

  dist=eigenvals[0]+rr00+rr11;

  Matrix<double> ddist_dm(4,4);

  Vector4d q(eigenvecs[0][0],eigenvecs[0][1],eigenvecs[0][2],eigenvecs[0][3]);

  Tensor dq_drr01[4];
  if(!alEqDis){
    double dq_dm[4][4][4];
    for(unsigned i=0;i<4;i++) for(unsigned j=0;j<4;j++) for(unsigned k=0;k<4;k++){
      double tmp=0.0;
// perturbation theory for matrix m
      for(unsigned l=1;l<4;l++) tmp+=eigenvecs[l][j]*eigenvecs[l][i]/(eigenvals[0]-eigenvals[l])*eigenvecs[0][k];
      dq_dm[i][j][k]=tmp;
    }
// propagation to _drr01
    for(unsigned i=0;i<4;i++){
      Tensor tmp;
      for(unsigned j=0;j<4;j++) for(unsigned k=0;k<4;k++) {
        tmp+=dq_dm[i][j][k]*dm_drr01[j][k];
      }
      dq_drr01[i]=tmp;
    }
  }

// This is the rotation matrix that brings reference to positions
// i.e. matmul(rotation,reference[iat])+shift is fitted to positions[iat]

  Tensor rotation;
  rotation[0][0]=q[0]*q[0]+q[1]*q[1]-q[2]*q[2]-q[3]*q[3];
  rotation[1][1]=q[0]*q[0]-q[1]*q[1]+q[2]*q[2]-q[3]*q[3];
  rotation[2][2]=q[0]*q[0]-q[1]*q[1]-q[2]*q[2]+q[3]*q[3];
  rotation[0][1]=2*(+q[0]*q[3]+q[1]*q[2]);
  rotation[0][2]=2*(-q[0]*q[2]+q[1]*q[3]);
  rotation[1][2]=2*(+q[0]*q[1]+q[2]*q[3]);
  rotation[1][0]=2*(-q[0]*q[3]+q[1]*q[2]);
  rotation[2][0]=2*(+q[0]*q[2]+q[1]*q[3]);
  rotation[2][1]=2*(-q[0]*q[1]+q[2]*q[3]);

  
  Tensor drotation_drr01[3][3];
  if(!alEqDis){
    drotation_drr01[0][0]=2*q[0]*dq_drr01[0]+2*q[1]*dq_drr01[1]-2*q[2]*dq_drr01[2]-2*q[3]*dq_drr01[3];
    drotation_drr01[1][1]=2*q[0]*dq_drr01[0]-2*q[1]*dq_drr01[1]+2*q[2]*dq_drr01[2]-2*q[3]*dq_drr01[3];
    drotation_drr01[2][2]=2*q[0]*dq_drr01[0]-2*q[1]*dq_drr01[1]-2*q[2]*dq_drr01[2]+2*q[3]*dq_drr01[3];
    drotation_drr01[0][1]=2*(+(q[0]*dq_drr01[3]+dq_drr01[0]*q[3])+(q[1]*dq_drr01[2]+dq_drr01[1]*q[2]));
    drotation_drr01[0][2]=2*(-(q[0]*dq_drr01[2]+dq_drr01[0]*q[2])+(q[1]*dq_drr01[3]+dq_drr01[1]*q[3]));
    drotation_drr01[1][2]=2*(+(q[0]*dq_drr01[1]+dq_drr01[0]*q[1])+(q[2]*dq_drr01[3]+dq_drr01[2]*q[3]));
    drotation_drr01[1][0]=2*(-(q[0]*dq_drr01[3]+dq_drr01[0]*q[3])+(q[1]*dq_drr01[2]+dq_drr01[1]*q[2]));
    drotation_drr01[2][0]=2*(+(q[0]*dq_drr01[2]+dq_drr01[0]*q[2])+(q[1]*dq_drr01[3]+dq_drr01[1]*q[3]));
    drotation_drr01[2][1]=2*(-(q[0]*dq_drr01[1]+dq_drr01[0]*q[1])+(q[2]*dq_drr01[3]+dq_drr01[2]*q[3]));
  }

  double prefactor=2.0;

  if(!squared && alEqDis) prefactor*=0.5/sqrt(dist);

// if "safe", recompute dist here to a better accuracy
  if(safe || !alEqDis) dist=0.0;

// If safe is set to "false", MSD is taken from the eigenvalue of the M matrix
// If safe is set to "true", MSD is recomputed from the rotational matrix
// For some reason, this last approach leads to less numerical noise but adds an overhead

  Tensor ddist_drotation;
  Vector ddist_dcpositions;

// third expensive loop: derivatives
  for(unsigned iat=0;iat<n;iat++){
    Vector d(positions[iat]-cpositions - matmul(rotation,reference[iat]));
    if(alEqDis){
// there is no need for derivatives of rotation and shift here as it is by construction zero
// (similar to Hellman-Feynman forces)
      derivatives[iat]= prefactor*align[iat]*d;
       if(safe) dist+=align[iat]*modulo2(d);
    } else {
// the case for align != displace is different, sob:
      dist+=displace[iat]*modulo2(d);
// these are the derivatives assuming the roto-translation as frozen
      derivatives[iat]=2*displace[iat]*d;
// here I accumulate derivatives wrt rotation matrix ..
      ddist_drotation+=-2*displace[iat]*extProduct(d,reference[iat]);
// .. and cpositions
      ddist_dcpositions+=-2*displace[iat]*d;
    }
  }

  if(!alEqDis){
    Tensor ddist_drr01;
    for(unsigned i=0;i<3;i++) for(unsigned j=0;j<3;j++) ddist_drr01+=ddist_drotation[i][j]*drotation_drr01[i][j];
    for(unsigned iat=0;iat<n;iat++){
// this is propagating to positions.
// I am implicitly using the derivative of rr01 wrt positions here
      derivatives[iat]+=matmul(ddist_drr01,reference[iat])*align[iat];
      derivatives[iat]+=ddist_dcpositions*align[iat];
    }
  }
  if(!squared){
    dist=sqrt(dist);
    if(!alEqDis){
      double xx=0.5/dist;
      for(unsigned iat=0;iat<n;iat++) derivatives[iat]*=xx;
    }
  }

  return dist;
}
#else
/// note that this method is intended to be repeatedly invoked 
/// when the reference does already have the center subtracted
/// but the position has not calculated center and not subtracted 
template <bool safe,bool alEqDis>
double RMSD::optimalAlignment(const  std::vector<double>  & align,
                              const  std::vector<double>  & displace,
                              const std::vector<Vector> & positions,
			      const std::vector<Vector> & reference ,
			      std::vector<Vector>  & derivatives,		
                              bool squared) const {
   plumed_massert(reference_center_is_calculated," at this point the reference center must be calculated"); 
   plumed_massert(reference_center_is_removed," at this point the reference center must be removed"); 
   //initialize the data into the structure
   RMSDCoreData cd(align,displace,positions,reference); 
   // transfer the settings for the center to let the CoreCalc deal with it 
   cd.setPositionsCenterIsRemoved(positions_center_is_removed);
   cd.setPositionsCenter(positions_center);
   cd.setReferenceCenterIsRemoved(reference_center_is_removed);
   cd.setPositionsCenter(positions_center);
   // Perform the diagonalization and all the needed stuff
   cd.doCoreCalc(safe,alEqDis); 
//   // make the core calc distance
//   double dist=cd.getDistance(squared); 
//   // make the derivatives by using pieces calculated in coreCalc (probably the best is just to copy the vector...) 
//   atom_ders=cd.getDDistanceDPositions(); 
//   return dist;    
      return 0.;
}

/// This calculates the elements needed by the quaternion to calculate everything that is needed
/// additional calls retrieve different components
/// note that this considers that the centers of both reference and positions are already setted by ctor
void RMSDCoreData::doCoreCalc(bool safe,bool alEqDis){

  const unsigned n=static_cast<unsigned int>(reference.size());
  
  plumed_massert(creference_is_calculated,"the center of the reference frame must be already provided at this stage"); 
  plumed_massert(cpositions_is_calculated,"the center of the positions frame must be already provided at this stage"); 

// This is the trace of positions*positions + reference*reference
  rr00=0.;
  rr11=0.;
// This is positions*reference
  Tensor rr01;
// center of mass managing: subtracted or not
  Vector cp; cp.zero(); if(!cpositions_is_removed)cp=cpositions;
  Vector cr; cr.zero(); if(!creference_is_removed)cr=creference;
// second expensive loop: compute second moments wrt centers
  for(unsigned iat=0;iat<n;iat++){
    double w=align[iat];
    rr00+=dotProduct(positions[iat]-cp,positions[iat]-cp)*w;
    rr11+=dotProduct(reference[iat]-cr,reference[iat]-cr)*w;
    rr01+=Tensor(positions[iat]-cp,reference[iat]-cr)*w;
  }

// the quaternion matrix: this is internal
  Matrix<double> m=Matrix<double>(4,4);

  m[0][0]=2.0*(-rr01[0][0]-rr01[1][1]-rr01[2][2]);
  m[1][1]=2.0*(-rr01[0][0]+rr01[1][1]+rr01[2][2]);
  m[2][2]=2.0*(+rr01[0][0]-rr01[1][1]+rr01[2][2]);
  m[3][3]=2.0*(+rr01[0][0]+rr01[1][1]-rr01[2][2]);
  m[0][1]=2.0*(-rr01[1][2]+rr01[2][1]);
  m[0][2]=2.0*(+rr01[0][2]-rr01[2][0]);
  m[0][3]=2.0*(-rr01[0][1]+rr01[1][0]);
  m[1][2]=2.0*(-rr01[0][1]-rr01[1][0]);
  m[1][3]=2.0*(-rr01[0][2]-rr01[2][0]);
  m[2][3]=2.0*(-rr01[1][2]-rr01[2][1]);
  m[1][0] = m[0][1];
  m[2][0] = m[0][2];
  m[2][1] = m[1][2];
  m[3][0] = m[0][3];
  m[3][1] = m[1][3];
  m[3][2] = m[2][3];

  
  Tensor dm_drr01[4][4];
  if(!alEqDis){
    dm_drr01[0][0] = 2.0*Tensor(-1.0, 0.0, 0.0,  0.0,-1.0, 0.0,  0.0, 0.0,-1.0); 
    dm_drr01[1][1] = 2.0*Tensor(-1.0, 0.0, 0.0,  0.0,+1.0, 0.0,  0.0, 0.0,+1.0);
    dm_drr01[2][2] = 2.0*Tensor(+1.0, 0.0, 0.0,  0.0,-1.0, 0.0,  0.0, 0.0,+1.0);
    dm_drr01[3][3] = 2.0*Tensor(+1.0, 0.0, 0.0,  0.0,+1.0, 0.0,  0.0, 0.0,-1.0);
    dm_drr01[0][1] = 2.0*Tensor( 0.0, 0.0, 0.0,  0.0, 0.0,-1.0,  0.0,+1.0, 0.0);
    dm_drr01[0][2] = 2.0*Tensor( 0.0, 0.0,+1.0,  0.0, 0.0, 0.0, -1.0, 0.0, 0.0);
    dm_drr01[0][3] = 2.0*Tensor( 0.0,-1.0, 0.0, +1.0, 0.0, 0.0,  0.0, 0.0, 0.0);
    dm_drr01[1][2] = 2.0*Tensor( 0.0,-1.0, 0.0, -1.0, 0.0, 0.0,  0.0, 0.0, 0.0);
    dm_drr01[1][3] = 2.0*Tensor( 0.0, 0.0,-1.0,  0.0, 0.0, 0.0, -1.0, 0.0, 0.0);
    dm_drr01[2][3] = 2.0*Tensor( 0.0, 0.0, 0.0,  0.0, 0.0,-1.0,  0.0,-1.0, 0.0);
    dm_drr01[1][0] = dm_drr01[0][1];
    dm_drr01[2][0] = dm_drr01[0][2];
    dm_drr01[2][1] = dm_drr01[1][2];
    dm_drr01[3][0] = dm_drr01[0][3];
    dm_drr01[3][1] = dm_drr01[1][3];
    dm_drr01[3][2] = dm_drr01[2][3];
  }


  int diagerror=diagMat(m, eigenvals, eigenvecs );

  if (diagerror!=0){
    std::string sdiagerror;
    Tools::convert(diagerror,sdiagerror);
    std::string msg="DIAGONALIZATION FAILED WITH ERROR CODE "+sdiagerror;
    plumed_merror(msg);
  }

  Vector4d q(eigenvecs[0][0],eigenvecs[0][1],eigenvecs[0][2],eigenvecs[0][3]);

  Tensor dq_drr01[4];
  if(!alEqDis){
    double dq_dm[4][4][4];
    for(unsigned i=0;i<4;i++) for(unsigned j=0;j<4;j++) for(unsigned k=0;k<4;k++){
      double tmp=0.0;
// perturbation theory for matrix m
      for(unsigned l=1;l<4;l++) tmp+=eigenvecs[l][j]*eigenvecs[l][i]/(eigenvals[0]-eigenvals[l])*eigenvecs[0][k];
      dq_dm[i][j][k]=tmp;
    }
// propagation to _drr01
    for(unsigned i=0;i<4;i++){
      Tensor tmp;
      for(unsigned j=0;j<4;j++) for(unsigned k=0;k<4;k++) {
        tmp+=dq_dm[i][j][k]*dm_drr01[j][k];
      }
      dq_drr01[i]=tmp;
    }
  }

// This is the rotation matrix that brings reference to positions
// i.e. matmul(rotation,reference[iat])+shift is fitted to positions[iat]

  rotation[0][0]=q[0]*q[0]+q[1]*q[1]-q[2]*q[2]-q[3]*q[3];
  rotation[1][1]=q[0]*q[0]-q[1]*q[1]+q[2]*q[2]-q[3]*q[3];
  rotation[2][2]=q[0]*q[0]-q[1]*q[1]-q[2]*q[2]+q[3]*q[3];
  rotation[0][1]=2*(+q[0]*q[3]+q[1]*q[2]);
  rotation[0][2]=2*(-q[0]*q[2]+q[1]*q[3]);
  rotation[1][2]=2*(+q[0]*q[1]+q[2]*q[3]);
  rotation[1][0]=2*(-q[0]*q[3]+q[1]*q[2]);
  rotation[2][0]=2*(+q[0]*q[2]+q[1]*q[3]);
  rotation[2][1]=2*(-q[0]*q[1]+q[2]*q[3]);
  
  if(!alEqDis){
    drotation_drr01[0][0]=2*q[0]*dq_drr01[0]+2*q[1]*dq_drr01[1]-2*q[2]*dq_drr01[2]-2*q[3]*dq_drr01[3];
    drotation_drr01[1][1]=2*q[0]*dq_drr01[0]-2*q[1]*dq_drr01[1]+2*q[2]*dq_drr01[2]-2*q[3]*dq_drr01[3];
    drotation_drr01[2][2]=2*q[0]*dq_drr01[0]-2*q[1]*dq_drr01[1]-2*q[2]*dq_drr01[2]+2*q[3]*dq_drr01[3];
    drotation_drr01[0][1]=2*(+(q[0]*dq_drr01[3]+dq_drr01[0]*q[3])+(q[1]*dq_drr01[2]+dq_drr01[1]*q[2]));
    drotation_drr01[0][2]=2*(-(q[0]*dq_drr01[2]+dq_drr01[0]*q[2])+(q[1]*dq_drr01[3]+dq_drr01[1]*q[3]));
    drotation_drr01[1][2]=2*(+(q[0]*dq_drr01[1]+dq_drr01[0]*q[1])+(q[2]*dq_drr01[3]+dq_drr01[2]*q[3]));
    drotation_drr01[1][0]=2*(-(q[0]*dq_drr01[3]+dq_drr01[0]*q[3])+(q[1]*dq_drr01[2]+dq_drr01[1]*q[2]));
    drotation_drr01[2][0]=2*(+(q[0]*dq_drr01[2]+dq_drr01[0]*q[2])+(q[1]*dq_drr01[3]+dq_drr01[1]*q[3]));
    drotation_drr01[2][1]=2*(-(q[0]*dq_drr01[1]+dq_drr01[0]*q[1])+(q[2]*dq_drr01[3]+dq_drr01[2]*q[3]));
  }

  d.resize(n);

  // calculate rotation matrix derivatives and components distances needed for components only when align!=displacement
  if(!alEqDis)ddist_drotation.zero();
  for(unsigned iat=0;iat<n;iat++){
    // components differences: this is useful externally
    d[iat]=positions[iat]-cp - matmul(rotation,reference[iat]-creference);	
    // ddist_drotation if needed
    if(!alEqDis) ddist_drotation+=-2*displace[iat]*extProduct(d[iat],reference[iat]-cr);
  }

  if(!alEqDis){
          ddist_drr01.zero();
          for(unsigned i=0;i<3;i++) for(unsigned j=0;j<3;j++) ddist_drr01+=ddist_drotation[i][j]*drotation_drr01[i][j];
  }
  // transfer this bools to the cd so that this settings will be reflected in the other calls
  this->alEqDis=alEqDis; 
  this->safe=safe; 
  isInitialized=true;

}
/// just retrieve the distance already calculated
double RMSDCoreData::getDistance(bool squared){

  if(!isInitialized)plumed_merror("OptimalRMSD.cpp cannot calculate the distance without being initialized first by doCoreCalc ");
  dist=eigenvals[0]+rr00+rr11;
  
  if(safe || !alEqDis) dist=0.0;
  const unsigned n=static_cast<unsigned int>(reference.size());
  for(unsigned iat=0;iat<n;iat++){
  	if(alEqDis){
  	    if(safe) dist+=align[iat]*modulo2(d[iat]);
  	} else {
  	    dist+=displace[iat]*modulo2(d[iat]);
  	}
  }
  if(!squared){
  	dist=sqrt(dist);
	distanceIsMSD=false;
  }else{
	distanceIsMSD=true;
  }
  hasDistance=true;
  return dist; 
}

std::vector<Vector> RMSDCoreData::getDDistanceDPositions(){
  std::vector<Vector>  derivatives;
  const unsigned n=static_cast<unsigned int>(reference.size());
  Vector ddist_dcpositions;
  derivatives.resize(n);
  double prefactor=2.0;
  if(!distanceIsMSD && alEqDis) prefactor*=0.5/dist;
  if(!hasDistance)plumed_merror("getDPositionsDerivatives needs to calculate the distance via getDistance first !");
  if(!isInitialized)plumed_merror("getDPositionsDerivatives needs to initialize the coreData first!");
  vector<Vector> ddist_tmp(n);
  Vector csum;
  Vector tmp1,tmp2;
  for(unsigned iat=0;iat<n;iat++){
    if(alEqDis){
// there is no need for derivatives of rotation and shift here as it is by construction zero
// (similar to Hellman-Feynman forces)
      derivatives[iat]= prefactor*align[iat]*d[iat];
    } else {
// these are the derivatives assuming the roto-translation as frozen
      tmp1=2*displace[iat]*d[iat];
      derivatives[iat]=tmp1;
// derivative of cpositions
      ddist_dcpositions+=-tmp1;
      // these needed for com corrections
      tmp2=matmul(ddist_drr01,reference[iat]-creference)*align[iat];	
      derivatives[iat]+=tmp2;	
      csum+=tmp2;
    }
  }

  if(!alEqDis){
    for(unsigned iat=0;iat<n;iat++)derivatives[iat]+=(ddist_dcpositions-csum)*align[iat]; 
  }
  if(!distanceIsMSD){
    if(!alEqDis){
      double xx=0.5/dist;
      for(unsigned iat=0;iat<n;iat++) derivatives[iat]*=xx;
    }
  }
  return derivatives;
}

std::vector<Vector>  RMSDCoreData::getDDistanceDReference(){
  std::vector<Vector>  derivatives;
  const unsigned n=static_cast<unsigned int>(reference.size());
  Vector ddist_dcreference;
  derivatives.resize(n);
  double prefactor=2.0;
  if(!distanceIsMSD && alEqDis) prefactor*=0.5/sqrt(dist);
  vector<Vector> ddist_tmp(n);
  Vector csum,tmp1,tmp2;

  if(!hasDistance)plumed_merror("getDDistanceDReference needs to calculate the distance via getDistance first !");
  if(!isInitialized)plumed_merror("getDDistanceDReference to initialize the coreData first!");
  // get the transpose rotation
  Tensor t_rotation=rotation.transpose();
  Tensor t_ddist_drr01=ddist_drr01.transpose();	
  
// third expensive loop: derivatives
  for(unsigned iat=0;iat<n;iat++){
    if(alEqDis){
// there is no need for derivatives of rotation and shift here as it is by construction zero
// (similar to Hellman-Feynman forces)
	//TODO: check this derivative down here
      derivatives[iat]= -prefactor*align[iat]*matmul(t_rotation,d[iat]);
    } else {
// these are the derivatives assuming the roto-translation as frozen
      tmp1=2*displace[iat]*matmul(t_rotation,d[iat]);
      derivatives[iat]= -tmp1;
// derivative of cpositions
      ddist_dcreference+=tmp1;
      // these below are needed for com correction
      tmp2=matmul(t_ddist_drr01,positions[iat]-cpositions)*align[iat];
      derivatives[iat]+=tmp2;	
      csum+=tmp2; 
    }
  }

  if(!alEqDis){
    for(unsigned iat=0;iat<n;iat++)derivatives[iat]+=(ddist_dcreference-csum)*align[iat]; 
  }
  if(!distanceIsMSD){
    if(!alEqDis){
      double xx=0.5/dist;
      for(unsigned iat=0;iat<n;iat++) derivatives[iat]*=xx;
    }
  }
  return derivatives;
}

/*
This below is the derivative of the rotation matrix that aligns the reference onto the positions
respect to positions
note that the this transformation overlap the  reference onto position
if inverseTransform=true then aligns the positions onto reference
*/
Matrix<std::vector<Vector> >  RMSDCoreData::getDRotationDPosition( bool inverseTransform ){
  const unsigned n=static_cast<unsigned int>(reference.size());
  if(!isInitialized)plumed_merror("getDRotationDPosition to initialize the coreData first!");
  Matrix<std::vector<Vector> > DRotDPos=Matrix<std::vector<Vector> >(3,3);  
  // remember drotation_drr01 is Tensor drotation_drr01[3][3]
  //           (3x3 rot) (3x3 components of rr01)    
  std::vector<Vector> v(n);
  Vector csum;
  // these below could probably be calculated in the main routine
  for(unsigned iat=0;iat<n;iat++) csum+=(reference[iat]-creference)*align[iat];
  for(unsigned iat=0;iat<n;iat++) v[iat]=(reference[iat]-creference-csum)*align[iat];
  for(unsigned a=0;a<3;a++){
  	for(unsigned b=0;b<3;b++){
		if(inverseTransform){
			DRotDPos[b][a].resize(n);
			for(unsigned iat=0;iat<n;iat++){
  			      DRotDPos[b][a][iat]=matmul(drotation_drr01[a][b],v[iat]);
  			}
		}else{
			DRotDPos[a][b].resize(n);
  			for(unsigned iat=0;iat<n;iat++){
  			      DRotDPos[a][b][iat]=matmul(drotation_drr01[a][b],v[iat]);
  			}
  		}
  	}
  }
  return DRotDPos;
}

/*
This below is the derivative of the rotation matrix that aligns the reference onto the positions
respect to reference
note that the this transformation overlap the  reference onto position
if inverseTransform=true then aligns the positions onto reference
*/
Matrix<std::vector<Vector> >  RMSDCoreData::getDRotationDReference( bool inverseTransform ){
  const unsigned n=static_cast<unsigned int>(reference.size());
  if(!isInitialized)plumed_merror("getDRotationDPosition to initialize the coreData first!");
  Matrix<std::vector<Vector> > DRotDRef=Matrix<std::vector<Vector> >(3,3);  
  // remember drotation_drr01 is Tensor drotation_drr01[3][3]
  //           (3x3 rot) (3x3 components of rr01)    
  std::vector<Vector> v(n);
  Vector csum;
  // these below could probably be calculated in the main routine
  for(unsigned iat=0;iat<n;iat++) csum+=(positions[iat]-cpositions)*align[iat];
  for(unsigned iat=0;iat<n;iat++) v[iat]=(positions[iat]-cpositions-csum)*align[iat];
 
  for(unsigned a=0;a<3;a++){
  	for(unsigned b=0;b<3;b++){
		Tensor t_drotation_drr01=drotation_drr01[a][b].transpose();
		if(inverseTransform){
			DRotDRef[b][a].resize(n);
			for(unsigned iat=0;iat<n;iat++){
  			      DRotDRef[b][a][iat]=matmul(t_drotation_drr01,v[iat]);
  			}
		}else{
			DRotDRef[a][b].resize(n);
  			for(unsigned iat=0;iat<n;iat++){
  			      DRotDRef[a][b][iat]=matmul(t_drotation_drr01,v[iat]);
  			}
  		}
  	}
  }
  return DRotDRef;
}


std::vector<Vector> RMSDCoreData::getAlignedReferenceToPositions(){
	  std::vector<Vector> alignedref;
	  const unsigned n=static_cast<unsigned int>(reference.size());
	  alignedref.resize(n);
	  if(!isInitialized)plumed_merror("getAlignedReferenceToPostions needs to initialize the coreData first!");
          // avoid to calculate matrix element but use the sum of what you have		  
	  for(unsigned iat=0;iat<n;iat++)alignedref[iat]=-d[iat]+positions[iat]-cpositions;
	  return alignedref; 
}
std::vector<Vector> RMSDCoreData::getAlignedPositionsToReference(){
	  std::vector<Vector> alignedpos;
	  const unsigned n=static_cast<unsigned int>(positions.size());
	  alignedpos.resize(n);
	  if(!isInitialized)plumed_merror("getAlignedPostionsToReference needs to initialize the coreData first!");
          // avoid to calculate matrix element but use the sum of what you have		  
	  for(unsigned iat=0;iat<n;iat++)alignedpos[iat]=matmul(rotation.transpose(),positions[iat]-cpositions);
	  return alignedpos; 
}


std::vector<Vector> RMSDCoreData::getCenteredPositions(){
	  std::vector<Vector> centeredpos;
	  const unsigned n=static_cast<unsigned int>(reference.size());
	  centeredpos.resize(n);
	  if(!isInitialized)plumed_merror("getCenteredPositions needs to initialize the coreData first!");
          // avoid to calculate matrix element but use the sum of what you have		  
	  for(unsigned iat=0;iat<n;iat++)centeredpos[iat]=positions[iat]-cpositions;
	  return centeredpos; 
}

std::vector<Vector> RMSDCoreData::getCenteredReference(){
	  std::vector<Vector> centeredref;
	  const unsigned n=static_cast<unsigned int>(reference.size());
	  centeredref.resize(n);
	  if(!isInitialized)plumed_merror("getCenteredReference needs to initialize the coreData first!");
          // avoid to calculate matrix element but use the sum of what you have		  
	  for(unsigned iat=0;iat<n;iat++)centeredref[iat]=reference[iat]-creference;
	  return centeredref; 
}




Tensor RMSDCoreData::getRotationMatrixReferenceToPositions(){
	  if(!isInitialized)plumed_merror("getRotationMatrixReferenceToPositions needs to initialize the coreData first!");
	  return rotation;
}

Tensor RMSDCoreData::getRotationMatrixPositionsToReference(){
	  if(!isInitialized)plumed_merror("getRotationMatrixReferenceToPositions needs to initialize the coreData first!");
	  return rotation.transpose();
}




#endif

}
