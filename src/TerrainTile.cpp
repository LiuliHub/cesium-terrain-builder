#include <string.h>             // for memcpy

#include "zlib.h"
#include "ogr_spatialref.h"

#include "TerrainTile.hpp"

TerrainTile::TerrainTile():
  mHeights(TILE_SIZE),
  mChildren(0)
{
  setIsLand();
}

TerrainTile::TerrainTile(const char *fileName):
  mHeights(TILE_SIZE)
{
  unsigned char inflateBuffer[MAX_TERRAIN_SIZE];
  unsigned int inflatedBytes;
  gzFile terrainFile = gzopen(fileName, "rb");

  if (terrainFile == NULL) {
    throw 1;
  }

  inflatedBytes = gzread(terrainFile, inflateBuffer, MAX_TERRAIN_SIZE);
  if (gzread(terrainFile, inflateBuffer, 1) != 0) {
    gzclose(terrainFile);
    throw 2;
  }
  gzclose(terrainFile);

  switch(inflatedBytes) {
  case MAX_TERRAIN_SIZE:      // a water mask is present
    mMaskLength = MASK_SIZE;
    break;
  case (TILE_SIZE * 2) + 2:   // there is no water mask
    mMaskLength = 1;
    break;
  default:                    // it can't be a terrain file
    throw 3;
  }

  short int byteCount = 0;
  for (short int i = 0; i < TILE_SIZE; i++, byteCount = i * 2) {
    mHeights[i] = inflateBuffer[byteCount] | (inflateBuffer[byteCount + 1]<<8);
  }

  mChildren = inflateBuffer[byteCount]; // byte 8451

  memcpy(mMask, &(inflateBuffer[++byteCount]), mMaskLength);
}

TerrainTile::TerrainTile(FILE *fp):
  mHeights(TILE_SIZE)
{
  unsigned char bytes[2];
  int count = 0;

  while ( count < TILE_SIZE && fread(bytes, 2, 1, fp) != 0) {
    /* adapted from
       <http://stackoverflow.com/questions/13001183/how-to-read-little-endian-integers-from-file-in-c> */
    mHeights[count++] = bytes[0] | (bytes[1]<<8);
  }

  if ( fread(&(mChildren), 1, 1, fp) != 1 ) {
    throw 1;
  }

  mMaskLength = fread(mMask, 1, MASK_SIZE, fp);
  switch (mMaskLength) {
  case MASK_SIZE:
    break;
  case 1:
    break;
  default:
    throw 2;
  }
}

void
TerrainTile::writeFile(FILE *fp) {
  fwrite(mHeights.data(), TILE_SIZE * 2, 1, fp);

  fwrite(&mChildren, 1, 1, fp);
  fwrite(mMask, mMaskLength, 1, fp);
}

void 
TerrainTile::writeFile(const char *fileName) {
  gzFile terrainFile = gzopen(fileName, "wb");

  if (terrainFile == NULL) {
    throw 1;
  }

  if (gzwrite(terrainFile, mHeights.data(), TILE_SIZE * 2) == 0) {
    gzclose(terrainFile);
    throw 2;
  }

  if (gzputc(terrainFile, mChildren) == -1) {
    gzclose(terrainFile);
    throw 3;
  }

  if (gzwrite(terrainFile, mMask, mMaskLength) == 0) {
    gzclose(terrainFile);
    throw 4;
  }

  switch (gzclose(terrainFile)) {
  case Z_OK:
    break;
  case Z_STREAM_ERROR:
  case Z_ERRNO:
  case Z_MEM_ERROR:
  case Z_BUF_ERROR:
  default:
    throw 5;
  }
}

std::vector<bool> TerrainTile::mask() {
  std::vector<bool> mask;
  mask.assign(mMask, mMask + mMaskLength);
  return mask;
}

/* for a discussion on bitflags see
   <http://www.dylanleigh.net/notes/c-cpp-tricks.html#Using_"Bitflags"> */
bool TerrainTile::hasChildren() {
  return mChildren;
}

bool TerrainTile::hasChildSW() {
  return ((mChildren & TC_SW) == TC_SW);
}

bool TerrainTile::hasChildSE() {
  return ((mChildren & TC_SE) == TC_SE);
}

bool TerrainTile::hasChildNW() {
  return ((mChildren & TC_NW) == TC_NW);
}

bool TerrainTile::hasChildNE() {
  return ((mChildren & TC_NE) == TC_NE);
}

void TerrainTile::setChildSW(bool on) {
  if (on) {
    mChildren |= TC_SW;
  } else {
    mChildren &= ~TC_SW;
  }
}

void TerrainTile::setChildSE(bool on) {
  if (on) {
    mChildren |= TC_SE;
  } else {
    mChildren &= ~TC_SE;
  }
}

void TerrainTile::setChildNW(bool on) {
  if (on) {
    mChildren |= TC_NW;
  } else {
    mChildren &= ~TC_NW;
  }
}

void TerrainTile::setChildNE(bool on) {
  if (on) {
    mChildren |= TC_NE;
  } else {
    mChildren &= ~TC_NE;
  }
}

void TerrainTile::setAllChildren(bool on) {
  if (on) {
    mChildren = TC_SW | TC_SE | TC_NW | TC_NE;
  } else {
    mChildren = 0;
  }
}

void TerrainTile::setIsWater() {
  mMask[0] = 1;
  mMaskLength = 1;
}

bool TerrainTile::isWater() {
  return mMaskLength == 1 && (bool) mMask[0];
}

void TerrainTile::setIsLand() {
  mMask[0] = 0;
  mMaskLength = 1;
}

bool TerrainTile::isLand() {
  return mMaskLength == 1 && ! (bool) mMask[0];
}

bool TerrainTile::hasWaterMask() {
  return mMaskLength == MASK_SIZE;
}

GDALDatasetH TerrainTile::heightsToRaster(double minx, double miny, double maxx, double maxy) {
  double resolution = (maxx - minx) / 65;
  double adfGeoTransform[6] = { minx, resolution, 0, maxy, 0, -resolution };

  OGRSpatialReference oSRS;
  oSRS.importFromEPSG(4326);

  GDALDriverH hDriver = GDALGetDriverByName( "MEM" );
  GDALDatasetH hDstDS;
  GDALRasterBandH hBand;

  char *pszDstWKT = NULL;
  oSRS.exportToWkt( &pszDstWKT );

  hDstDS = GDALCreate(hDriver, "", 65, 65, 1, GDT_Int16, NULL );
  GDALSetProjection( hDstDS, pszDstWKT );
  CPLFree( pszDstWKT );
  GDALSetGeoTransform( hDstDS, adfGeoTransform );

  hBand = GDALGetRasterBand( hDstDS, 1 );
  GDALRasterIO( hBand, GF_Write, 0, 0, 65, 65,
                mHeights.data(), 65, 65, GDT_Int16, 0, 0 );

  return hDstDS;
}