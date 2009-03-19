#pragma region License
/******************************************************************************
This file is part of the PolyVox library
Copyright (C) 2006  David Williams

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
******************************************************************************/
#pragma endregion

#include "PolyVoxCore/PolyVoxImpl/DecimatedSurfaceExtractor.h"

#include "PolyVoxCore/Volume.h"
#include "PolyVoxCore/GradientEstimators.h"
#include "PolyVoxCore/IndexedSurfacePatch.h"
#include "PolyVoxCore/MarchingCubesTables.h"
#include "PolyVoxCore/Region.h"
#include "PolyVoxCore/VolumeIterator.h"

#include <algorithm>

using namespace std;

namespace PolyVox
{
	uint32 getDecimatedIndex(uint32 x, uint32 y)
	{
		return x + (y * (POLYVOX_REGION_SIDE_LENGTH+1));
	}

	void extractDecimatedSurfaceImpl(Volume<uint8>* volumeData, uint8 uLevel, Region region, IndexedSurfacePatch* singleMaterialPatch)
	{	
		singleMaterialPatch->clear();

		//For edge indices
		int32* vertexIndicesX0 = new int32[(POLYVOX_REGION_SIDE_LENGTH+1) * (POLYVOX_REGION_SIDE_LENGTH+1)];
		int32* vertexIndicesY0 = new int32[(POLYVOX_REGION_SIDE_LENGTH+1) * (POLYVOX_REGION_SIDE_LENGTH+1)];
		int32* vertexIndicesZ0 = new int32[(POLYVOX_REGION_SIDE_LENGTH+1) * (POLYVOX_REGION_SIDE_LENGTH+1)];
		int32* vertexIndicesX1 = new int32[(POLYVOX_REGION_SIDE_LENGTH+1) * (POLYVOX_REGION_SIDE_LENGTH+1)];
		int32* vertexIndicesY1 = new int32[(POLYVOX_REGION_SIDE_LENGTH+1) * (POLYVOX_REGION_SIDE_LENGTH+1)];
		int32* vertexIndicesZ1 = new int32[(POLYVOX_REGION_SIDE_LENGTH+1) * (POLYVOX_REGION_SIDE_LENGTH+1)];

		//Cell bitmasks
		uint8* bitmask0 = new uint8[(POLYVOX_REGION_SIDE_LENGTH+1) * (POLYVOX_REGION_SIDE_LENGTH+1)];
		uint8* bitmask1 = new uint8[(POLYVOX_REGION_SIDE_LENGTH+1) * (POLYVOX_REGION_SIDE_LENGTH+1)];

		const uint8 uStepSize = uLevel == 0 ? 1 : 1 << uLevel;

		//When generating the mesh for a region we actually look outside it in the
		// back, bottom, right direction. Protect against access violations by cropping region here
		Region regVolume = volumeData->getEnclosingRegion();
		regVolume.setUpperCorner(regVolume.getUpperCorner() - Vector3DInt32(2*uStepSize-1,2*uStepSize-1,2*uStepSize-1));
		region.cropTo(regVolume);

		//Offset from volume corner
		const Vector3DFloat offset = static_cast<Vector3DFloat>(region.getLowerCorner());

		//Create a region corresponding to the first slice
		Region regSlice0(region);
		Vector3DInt32 v3dUpperCorner = regSlice0.getUpperCorner();
		v3dUpperCorner.setZ(regSlice0.getLowerCorner().getZ()); //Set the upper z to the lower z to make it one slice thick.
		regSlice0.setUpperCorner(v3dUpperCorner);
		
		//Iterator to access the volume data
		VolumeIterator<uint8> volIter(*volumeData);		

		//Compute bitmask for initial slice
		uint32 uNoOfNonEmptyCellsForSlice0 = computeInitialDecimatedBitmaskForSlice(volIter, uLevel, regSlice0, offset, bitmask0);		
		if(uNoOfNonEmptyCellsForSlice0 != 0)
		{
			//If there were some non-empty cells then generate initial slice vertices for them
			generateDecimatedVerticesForSlice(volIter, uLevel, regSlice0, offset, bitmask0, singleMaterialPatch, vertexIndicesX0, vertexIndicesY0, vertexIndicesZ0);
		}

		for(uint32 uSlice = 1; ((uSlice <= POLYVOX_REGION_SIDE_LENGTH) && (uSlice + offset.getZ() <= regVolume.getUpperCorner().getZ())); uSlice += uStepSize)
		{
			Region regSlice1(regSlice0);
			regSlice1.shift(Vector3DInt32(0,0,uStepSize));

			uint32 uNoOfNonEmptyCellsForSlice1 = computeDecimatedBitmaskForSliceFromPrevious(volIter, uLevel, regSlice1, offset, bitmask1, bitmask0);

			if(uNoOfNonEmptyCellsForSlice1 != 0)
			{
				generateDecimatedVerticesForSlice(volIter, uLevel, regSlice1, offset, bitmask1, singleMaterialPatch, vertexIndicesX1, vertexIndicesY1, vertexIndicesZ1);				
			}

			if((uNoOfNonEmptyCellsForSlice0 != 0) || (uNoOfNonEmptyCellsForSlice1 != 0))
			{
				generateDecimatedIndicesForSlice(volIter, uLevel, regSlice0, singleMaterialPatch, offset, bitmask0, bitmask1, vertexIndicesX0, vertexIndicesY0, vertexIndicesZ0, vertexIndicesX1, vertexIndicesY1, vertexIndicesZ1);
			}

			std::swap(uNoOfNonEmptyCellsForSlice0, uNoOfNonEmptyCellsForSlice1);
			std::swap(bitmask0, bitmask1);
			std::swap(vertexIndicesX0, vertexIndicesX1);
			std::swap(vertexIndicesY0, vertexIndicesY1);
			std::swap(vertexIndicesZ0, vertexIndicesZ1);

			regSlice0 = regSlice1;
		}

		delete[] bitmask0;
		delete[] bitmask1;
		delete[] vertexIndicesX0;
		delete[] vertexIndicesX1;
		delete[] vertexIndicesY0;
		delete[] vertexIndicesY1;
		delete[] vertexIndicesZ0;
		delete[] vertexIndicesZ1;


		/*std::vector<SurfaceVertex>::iterator iterSurfaceVertex = singleMaterialPatch->getVertices().begin();
		while(iterSurfaceVertex != singleMaterialPatch->getVertices().end())
		{
			Vector3DFloat tempNormal = computeDecimatedNormal(volumeData, static_cast<Vector3DFloat>(iterSurfaceVertex->getPosition() + offset), CENTRAL_DIFFERENCE);
			const_cast<SurfaceVertex&>(*iterSurfaceVertex).setNormal(tempNormal);
			++iterSurfaceVertex;
		}*/
	}

	uint32 computeInitialDecimatedBitmaskForSlice(VolumeIterator<uint8>& volIter, uint8 uLevel,  const Region& regSlice, const Vector3DFloat& offset, uint8* bitmask)
	{
		const uint8 uStepSize = uLevel == 0 ? 1 : 1 << uLevel;
		uint32 uNoOfNonEmptyCells = 0;

		//Iterate over each cell in the region
		for(uint16 y = regSlice.getLowerCorner().getY(); y <= regSlice.getUpperCorner().getY(); y += uStepSize)
		{
			for(uint16 x = regSlice.getLowerCorner().getX(); x <= regSlice.getUpperCorner().getX(); x += uStepSize)
			{		
				//Current position
				volIter.setPosition(x,y,regSlice.getLowerCorner().getZ());

				//Determine the index into the edge table which tells us which vertices are inside of the surface
				uint8 iCubeIndex = 0;

				if((x==regSlice.getLowerCorner().getX()) && (y==regSlice.getLowerCorner().getY()))
				{
					volIter.setPosition(x,y,regSlice.getLowerCorner().getZ());
					const uint8 v000 = volIter.getSubSampledVoxel(uLevel);
					volIter.setPosition(x+uStepSize,y,regSlice.getLowerCorner().getZ());
					const uint8 v100 = volIter.getSubSampledVoxel(uLevel);
					volIter.setPosition(x,y+uStepSize,regSlice.getLowerCorner().getZ());
					const uint8 v010 = volIter.getSubSampledVoxel(uLevel);
					volIter.setPosition(x+uStepSize,y+uStepSize,regSlice.getLowerCorner().getZ());
					const uint8 v110 = volIter.getSubSampledVoxel(uLevel);

					volIter.setPosition(x,y,regSlice.getLowerCorner().getZ()+uStepSize);
					const uint8 v001 = volIter.getSubSampledVoxel(uLevel);
					volIter.setPosition(x+uStepSize,y,regSlice.getLowerCorner().getZ()+uStepSize);
					const uint8 v101 = volIter.getSubSampledVoxel(uLevel);
					volIter.setPosition(x,y+uStepSize,regSlice.getLowerCorner().getZ()+uStepSize);
					const uint8 v011 = volIter.getSubSampledVoxel(uLevel);
					volIter.setPosition(x+uStepSize,y+uStepSize,regSlice.getLowerCorner().getZ()+uStepSize);
					const uint8 v111 = volIter.getSubSampledVoxel(uLevel);		

					if (v000 == 0) iCubeIndex |= 1;
					if (v100 == 0) iCubeIndex |= 2;
					if (v110 == 0) iCubeIndex |= 4;
					if (v010 == 0) iCubeIndex |= 8;
					if (v001 == 0) iCubeIndex |= 16;
					if (v101 == 0) iCubeIndex |= 32;
					if (v111 == 0) iCubeIndex |= 64;
					if (v011 == 0) iCubeIndex |= 128;
				}
				else if((x>regSlice.getLowerCorner().getX()) && y==regSlice.getLowerCorner().getY())
				{
					volIter.setPosition(x+uStepSize,y,regSlice.getLowerCorner().getZ());
					const uint8 v100 = volIter.getSubSampledVoxel(uLevel);
					volIter.setPosition(x+uStepSize,y+uStepSize,regSlice.getLowerCorner().getZ());
					const uint8 v110 = volIter.getSubSampledVoxel(uLevel);

					volIter.setPosition(x+uStepSize,y,regSlice.getLowerCorner().getZ()+uStepSize);
					const uint8 v101 = volIter.getSubSampledVoxel(uLevel);
					volIter.setPosition(x+uStepSize,y+uStepSize,regSlice.getLowerCorner().getZ()+uStepSize);
					const uint8 v111 = volIter.getSubSampledVoxel(uLevel);

					//x
					uint8 iPreviousCubeIndexX = bitmask[getDecimatedIndex(x- offset.getX()-uStepSize,y- offset.getY())];
					uint8 srcBit6 = iPreviousCubeIndexX & 64;
					uint8 destBit7 = srcBit6 << 1;
					
					uint8 srcBit5 = iPreviousCubeIndexX & 32;
					uint8 destBit4 = srcBit5 >> 1;

					uint8 srcBit2 = iPreviousCubeIndexX & 4;
					uint8 destBit3 = srcBit2 << 1;
					
					uint8 srcBit1 = iPreviousCubeIndexX & 2;
					uint8 destBit0 = srcBit1 >> 1;

					iCubeIndex |= destBit0;
					if (v100 == 0) iCubeIndex |= 2;
					if (v110 == 0) iCubeIndex |= 4;
					iCubeIndex |= destBit3;
					iCubeIndex |= destBit4;
					if (v101 == 0) iCubeIndex |= 32;
					if (v111 == 0) iCubeIndex |= 64;
					iCubeIndex |= destBit7;
				}
				else if((x==regSlice.getLowerCorner().getX()) && (y>regSlice.getLowerCorner().getY()))
				{
					volIter.setPosition(x,y+uStepSize,regSlice.getLowerCorner().getZ());
					const uint8 v010 = volIter.getSubSampledVoxel(uLevel);
					volIter.setPosition(x+uStepSize,y+uStepSize,regSlice.getLowerCorner().getZ());
					const uint8 v110 = volIter.getSubSampledVoxel(uLevel);

					volIter.setPosition(x,y+uStepSize,regSlice.getLowerCorner().getZ()+uStepSize);
					const uint8 v011 = volIter.getSubSampledVoxel(uLevel);
					volIter.setPosition(x+uStepSize,y+uStepSize,regSlice.getLowerCorner().getZ()+uStepSize);
					const uint8 v111 = volIter.getSubSampledVoxel(uLevel);

					//y
					uint8 iPreviousCubeIndexY = bitmask[getDecimatedIndex(x- offset.getX(),y- offset.getY()-uStepSize)];
					uint8 srcBit7 = iPreviousCubeIndexY & 128;
					uint8 destBit4 = srcBit7 >> 3;
					
					uint8 srcBit6 = iPreviousCubeIndexY & 64;
					uint8 destBit5 = srcBit6 >> 1;

					uint8 srcBit3 = iPreviousCubeIndexY & 8;
					uint8 destBit0 = srcBit3 >> 3;
					
					uint8 srcBit2 = iPreviousCubeIndexY & 4;
					uint8 destBit1 = srcBit2 >> 1;

					iCubeIndex |= destBit0;
					iCubeIndex |= destBit1;
					if (v110 == 0) iCubeIndex |= 4;
					if (v010 == 0) iCubeIndex |= 8;
					iCubeIndex |= destBit4;
					iCubeIndex |= destBit5;
					if (v111 == 0) iCubeIndex |= 64;
					if (v011 == 0) iCubeIndex |= 128;
				}
				else
				{
					volIter.setPosition(x+uStepSize,y+uStepSize,regSlice.getLowerCorner().getZ());
					const uint8 v110 = volIter.getSubSampledVoxel(uLevel);

					volIter.setPosition(x+uStepSize,y+uStepSize,regSlice.getLowerCorner().getZ()+uStepSize);
					const uint8 v111 = volIter.getSubSampledVoxel(uLevel);

					//y
					uint8 iPreviousCubeIndexY = bitmask[getDecimatedIndex(x- offset.getX(),y- offset.getY()-uStepSize)];
					uint8 srcBit7 = iPreviousCubeIndexY & 128;
					uint8 destBit4 = srcBit7 >> 3;
					
					uint8 srcBit6 = iPreviousCubeIndexY & 64;
					uint8 destBit5 = srcBit6 >> 1;

					uint8 srcBit3 = iPreviousCubeIndexY & 8;
					uint8 destBit0 = srcBit3 >> 3;
					
					uint8 srcBit2 = iPreviousCubeIndexY & 4;
					uint8 destBit1 = srcBit2 >> 1;

					//x
					uint8 iPreviousCubeIndexX = bitmask[getDecimatedIndex(x- offset.getX()-uStepSize,y- offset.getY())];
					srcBit6 = iPreviousCubeIndexX & 64;
					uint8 destBit7 = srcBit6 << 1;

					srcBit2 = iPreviousCubeIndexX & 4;
					uint8 destBit3 = srcBit2 << 1;

					iCubeIndex |= destBit0;
					iCubeIndex |= destBit1;
					if (v110 == 0) iCubeIndex |= 4;
					iCubeIndex |= destBit3;
					iCubeIndex |= destBit4;
					iCubeIndex |= destBit5;
					if (v111 == 0) iCubeIndex |= 64;
					iCubeIndex |= destBit7;
				}

				//Save the bitmask
				bitmask[getDecimatedIndex(x- offset.getX(),y- offset.getY())] = iCubeIndex;

				if(edgeTable[iCubeIndex] != 0)
				{
					++uNoOfNonEmptyCells;
				}
				
			}
		}

		return uNoOfNonEmptyCells;
	}

	uint32 computeDecimatedBitmaskForSliceFromPrevious(VolumeIterator<uint8>& volIter, uint8 uLevel, const Region& regSlice, const Vector3DFloat& offset, uint8* bitmask, uint8* previousBitmask)
	{
		const uint8 uStepSize = uLevel == 0 ? 1 : 1 << uLevel;
		uint32 uNoOfNonEmptyCells = 0;

		//Iterate over each cell in the region
		for(uint16 y = regSlice.getLowerCorner().getY(); y <= regSlice.getUpperCorner().getY(); y += uStepSize)
		{
			for(uint16 x = regSlice.getLowerCorner().getX(); x <= regSlice.getUpperCorner().getX(); x += uStepSize)
			{	
				//Current position
				volIter.setPosition(x,y,regSlice.getLowerCorner().getZ());

				//Determine the index into the edge table which tells us which vertices are inside of the surface
				uint8 iCubeIndex = 0;

				if((x==regSlice.getLowerCorner().getX()) && (y==regSlice.getLowerCorner().getY()))
				{
					volIter.setPosition(x,y,regSlice.getLowerCorner().getZ()+uStepSize);
					const uint8 v001 = volIter.getSubSampledVoxel(uLevel);
					volIter.setPosition(x+uStepSize,y,regSlice.getLowerCorner().getZ()+uStepSize);
					const uint8 v101 = volIter.getSubSampledVoxel(uLevel);
					volIter.setPosition(x,y+uStepSize,regSlice.getLowerCorner().getZ()+uStepSize);
					const uint8 v011 = volIter.getSubSampledVoxel(uLevel);
					volIter.setPosition(x+uStepSize,y+uStepSize,regSlice.getLowerCorner().getZ()+uStepSize);
					const uint8 v111 = volIter.getSubSampledVoxel(uLevel);	

					//z
					uint8 iPreviousCubeIndexZ = previousBitmask[getDecimatedIndex(x- offset.getX(),y- offset.getY())];
					iCubeIndex = iPreviousCubeIndexZ >> 4;

					if (v001 == 0) iCubeIndex |= 16;
					if (v101 == 0) iCubeIndex |= 32;
					if (v111 == 0) iCubeIndex |= 64;
					if (v011 == 0) iCubeIndex |= 128;
				}
				else if((x>regSlice.getLowerCorner().getX()) && y==regSlice.getLowerCorner().getY())
				{
					volIter.setPosition(x+uStepSize,y,regSlice.getLowerCorner().getZ()+uStepSize);
					const uint8 v101 = volIter.getSubSampledVoxel(uLevel);
					volIter.setPosition(x+uStepSize,y+uStepSize,regSlice.getLowerCorner().getZ()+uStepSize);
					const uint8 v111 = volIter.getSubSampledVoxel(uLevel);	

					//z
					uint8 iPreviousCubeIndexZ = previousBitmask[getDecimatedIndex(x- offset.getX(),y- offset.getY())];
					iCubeIndex = iPreviousCubeIndexZ >> 4;

					//x
					uint8 iPreviousCubeIndexX = bitmask[getDecimatedIndex(x- offset.getX()-uStepSize,y- offset.getY())];
					uint8 srcBit6 = iPreviousCubeIndexX & 64;
					uint8 destBit7 = srcBit6 << 1;
					
					uint8 srcBit5 = iPreviousCubeIndexX & 32;
					uint8 destBit4 = srcBit5 >> 1;

					iCubeIndex |= destBit4;
					if (v101 == 0) iCubeIndex |= 32;
					if (v111 == 0) iCubeIndex |= 64;
					iCubeIndex |= destBit7;
				}
				else if((x==regSlice.getLowerCorner().getX()) && (y>regSlice.getLowerCorner().getY()))
				{
					volIter.setPosition(x,y+uStepSize,regSlice.getLowerCorner().getZ()+uStepSize);
					const uint8 v011 = volIter.getSubSampledVoxel(uLevel);
					volIter.setPosition(x+uStepSize,y+uStepSize,regSlice.getLowerCorner().getZ()+uStepSize);
					const uint8 v111 = volIter.getSubSampledVoxel(uLevel);	

					//z
					uint8 iPreviousCubeIndexZ = previousBitmask[getDecimatedIndex(x- offset.getX(),y- offset.getY())];
					iCubeIndex = iPreviousCubeIndexZ >> 4;

					//y
					uint8 iPreviousCubeIndexY = bitmask[getDecimatedIndex(x- offset.getX(),y- offset.getY()-uStepSize)];
					uint8 srcBit7 = iPreviousCubeIndexY & 128;
					uint8 destBit4 = srcBit7 >> 3;
					
					uint8 srcBit6 = iPreviousCubeIndexY & 64;
					uint8 destBit5 = srcBit6 >> 1;

					iCubeIndex |= destBit4;
					iCubeIndex |= destBit5;
					if (v111 == 0) iCubeIndex |= 64;
					if (v011 == 0) iCubeIndex |= 128;
				}
				else
				{
					volIter.setPosition(x+uStepSize,y+uStepSize,regSlice.getLowerCorner().getZ()+uStepSize);
					const uint8 v111 = volIter.getSubSampledVoxel(uLevel);	

					//z
					uint8 iPreviousCubeIndexZ = previousBitmask[getDecimatedIndex(x- offset.getX(),y- offset.getY())];
					iCubeIndex = iPreviousCubeIndexZ >> 4;

					//y
					uint8 iPreviousCubeIndexY = bitmask[getDecimatedIndex(x- offset.getX(),y- offset.getY()-uStepSize)];
					uint8 srcBit7 = iPreviousCubeIndexY & 128;
					uint8 destBit4 = srcBit7 >> 3;
					
					uint8 srcBit6 = iPreviousCubeIndexY & 64;
					uint8 destBit5 = srcBit6 >> 1;

					//x
					uint8 iPreviousCubeIndexX = bitmask[getDecimatedIndex(x- offset.getX()-uStepSize,y- offset.getY())];
					srcBit6 = iPreviousCubeIndexX & 64;
					uint8 destBit7 = srcBit6 << 1;

					iCubeIndex |= destBit4;
					iCubeIndex |= destBit5;
					if (v111 == 0) iCubeIndex |= 64;
					iCubeIndex |= destBit7;
				}

				//Save the bitmask
				bitmask[getDecimatedIndex(x- offset.getX(),y- offset.getY())] = iCubeIndex;

				if(edgeTable[iCubeIndex] != 0)
				{
					++uNoOfNonEmptyCells;
				}
				
			}//For each cell
		}

		return uNoOfNonEmptyCells;
	}

	void generateDecimatedVerticesForSlice(VolumeIterator<uint8>& volIter, uint8 uLevel, Region& regSlice, const Vector3DFloat& offset, uint8* bitmask, IndexedSurfacePatch* singleMaterialPatch,int32 vertexIndicesX[],int32 vertexIndicesY[],int32 vertexIndicesZ[])
	{
		const uint8 uStepSize = uLevel == 0 ? 1 : 1 << uLevel;

		//Iterate over each cell in the region
		for(uint16 y = regSlice.getLowerCorner().getY(); y <= regSlice.getUpperCorner().getY(); y += uStepSize)
		{
			for(uint16 x = regSlice.getLowerCorner().getX(); x <= regSlice.getUpperCorner().getX(); x += uStepSize)
			{		
				//Current position
				const uint16 z = regSlice.getLowerCorner().getZ();

				volIter.setPosition(x,y,z);
				const uint8 v000 = volIter.getSubSampledVoxel(uLevel);

				//Determine the index into the edge table which tells us which vertices are inside of the surface
				uint8 iCubeIndex = bitmask[getDecimatedIndex(x - offset.getX(),y - offset.getY())];

				/* Cube is entirely in/out of the surface */
				if (edgeTable[iCubeIndex] == 0)
				{
					continue;
				}

				/* Find the vertices where the surface intersects the cube */
				if (edgeTable[iCubeIndex] & 1)
				{
					if(x != regSlice.getUpperCorner().getX())
					{
						volIter.setPosition(x + uStepSize,y,z);
						const uint8 v100 = volIter.getSubSampledVoxel(uLevel);
						const Vector3DFloat v3dPosition(x - offset.getX() + 0.5f * uStepSize, y - offset.getY(), z - offset.getZ());
						const Vector3DFloat v3dNormal(v000 > v100 ? 1.0f : -1.0f,0.0,0.0);
						const uint8 uMaterial = v000 | v100; //Because one of these is 0, the or operation takes the max.
						SurfaceVertex surfaceVertex(v3dPosition, v3dNormal, uMaterial);
						uint32 uLastVertexIndex = singleMaterialPatch->addVertex(surfaceVertex);
						vertexIndicesX[getDecimatedIndex(x - offset.getX(),y - offset.getY())] = uLastVertexIndex;
					}
				}
				if (edgeTable[iCubeIndex] & 8)
				{
					if(y != regSlice.getUpperCorner().getY())
					{
						volIter.setPosition(x,y + uStepSize,z);
						const uint8 v010 = volIter.getSubSampledVoxel(uLevel);
						const Vector3DFloat v3dPosition(x - offset.getX(), y - offset.getY() + 0.5f * uStepSize, z - offset.getZ());
						const Vector3DFloat v3dNormal(0.0,v000 > v010 ? 1.0f : -1.0f,0.0);
						const uint8 uMaterial = v000 | v010; //Because one of these is 0, the or operation takes the max.
						SurfaceVertex surfaceVertex(v3dPosition, v3dNormal, uMaterial);
						uint32 uLastVertexIndex = singleMaterialPatch->addVertex(surfaceVertex);
						vertexIndicesY[getDecimatedIndex(x - offset.getX(),y - offset.getY())] = uLastVertexIndex;
					}
				}
				if (edgeTable[iCubeIndex] & 256)
				{
					//if(z != regSlice.getUpperCorner.getZ())
					{
						volIter.setPosition(x,y,z + uStepSize);
						const uint8 v001 = volIter.getSubSampledVoxel(uLevel);
						const Vector3DFloat v3dPosition(x - offset.getX(), y - offset.getY(), z - offset.getZ() + 0.5f * uStepSize);
						const Vector3DFloat v3dNormal(0.0,0.0,v000 > v001 ? 1.0f : -1.0f);
						const uint8 uMaterial = v000 | v001; //Because one of these is 0, the or operation takes the max.
						const SurfaceVertex surfaceVertex(v3dPosition, v3dNormal, uMaterial);
						uint32 uLastVertexIndex = singleMaterialPatch->addVertex(surfaceVertex);
						vertexIndicesZ[getDecimatedIndex(x - offset.getX(),y - offset.getY())] = uLastVertexIndex;
					}
				}
			}//For each cell
		}
	}

	void generateDecimatedIndicesForSlice(VolumeIterator<uint8>& volIter, uint8 uLevel, const Region& regSlice, IndexedSurfacePatch* singleMaterialPatch, const Vector3DFloat& offset, uint8* bitmask0, uint8* bitmask1, int32 vertexIndicesX0[],int32 vertexIndicesY0[],int32 vertexIndicesZ0[], int32 vertexIndicesX1[],int32 vertexIndicesY1[],int32 vertexIndicesZ1[])
	{
		const uint8 uStepSize = uLevel == 0 ? 1 : 1 << uLevel;
		uint32 indlist[12];

		for(uint16 y = regSlice.getLowerCorner().getY() - offset.getY(); y < regSlice.getUpperCorner().getY() - offset.getY(); y += uStepSize)
		{
			for(uint16 x = regSlice.getLowerCorner().getX() - offset.getX(); x < regSlice.getUpperCorner().getX() - offset.getX(); x += uStepSize)
			{		
				//Current position
				const uint16 z = regSlice.getLowerCorner().getZ() - offset.getZ();

				//Determine the index into the edge table which tells us which vertices are inside of the surface
				uint8 iCubeIndex = bitmask0[getDecimatedIndex(x,y)];

				/* Cube is entirely in/out of the surface */
				if (edgeTable[iCubeIndex] == 0)
				{
					continue;
				}

				/* Find the vertices where the surface intersects the cube */
				if (edgeTable[iCubeIndex] & 1)
				{
					indlist[0] = vertexIndicesX0[getDecimatedIndex(x,y)];
					assert(indlist[0] != -1);
				}
				if (edgeTable[iCubeIndex] & 2)
				{
					indlist[1] = vertexIndicesY0[getDecimatedIndex(x+uStepSize,y)];
					assert(indlist[1] != -1);
				}
				if (edgeTable[iCubeIndex] & 4)
				{
					indlist[2] = vertexIndicesX0[getDecimatedIndex(x,y+uStepSize)];
					assert(indlist[2] != -1);
				}
				if (edgeTable[iCubeIndex] & 8)
				{
					indlist[3] = vertexIndicesY0[getDecimatedIndex(x,y)];
					assert(indlist[3] != -1);
				}
				if (edgeTable[iCubeIndex] & 16)
				{
					indlist[4] = vertexIndicesX1[getDecimatedIndex(x,y)];
					assert(indlist[4] != -1);
				}
				if (edgeTable[iCubeIndex] & 32)
				{
					indlist[5] = vertexIndicesY1[getDecimatedIndex(x+uStepSize,y)];
					assert(indlist[5] != -1);
				}
				if (edgeTable[iCubeIndex] & 64)
				{
					indlist[6] = vertexIndicesX1[getDecimatedIndex(x,y+uStepSize)];
					assert(indlist[6] != -1);
				}
				if (edgeTable[iCubeIndex] & 128)
				{
					indlist[7] = vertexIndicesY1[getDecimatedIndex(x,y)];
					assert(indlist[7] != -1);
				}
				if (edgeTable[iCubeIndex] & 256)
				{
					indlist[8] = vertexIndicesZ0[getDecimatedIndex(x,y)];
					assert(indlist[8] != -1);
				}
				if (edgeTable[iCubeIndex] & 512)
				{
					indlist[9] = vertexIndicesZ0[getDecimatedIndex(x+uStepSize,y)];
					assert(indlist[9] != -1);
				}
				if (edgeTable[iCubeIndex] & 1024)
				{
					indlist[10] = vertexIndicesZ0[getDecimatedIndex(x+uStepSize,y+uStepSize)];
					assert(indlist[10] != -1);
				}
				if (edgeTable[iCubeIndex] & 2048)
				{
					indlist[11] = vertexIndicesZ0[getDecimatedIndex(x,y+uStepSize)];
					assert(indlist[11] != -1);
				}

				for (int i=0;triTable[iCubeIndex][i]!=-1;i+=3)
				{
					uint32 ind0 = indlist[triTable[iCubeIndex][i  ]];
					uint32 ind1 = indlist[triTable[iCubeIndex][i+1]];
					uint32 ind2 = indlist[triTable[iCubeIndex][i+2]];

					singleMaterialPatch->addTriangle(ind0, ind1, ind2);
				}//For each triangle
			}//For each cell
		}
	}
}