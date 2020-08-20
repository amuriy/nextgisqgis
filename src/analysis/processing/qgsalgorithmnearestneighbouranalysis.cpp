/***************************************************************************
                         qgsalgorithmnearestneighbouranalysis.cpp
                         ---------------------
    begin                : December 2019
    copyright            : (C) 2019 by Alexander Bruy
    email                : alexander dot bruy at gmail dot com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsalgorithmnearestneighbouranalysis.h"
#include "qgsapplication.h"

///@cond PRIVATE

QString QgsNearestNeighbourAnalysisAlgorithm::name() const
{
  return QStringLiteral( "nearestneighbouranalysis" );
}

QString QgsNearestNeighbourAnalysisAlgorithm::displayName() const
{
  return QObject::tr( "Nearest neighbour analysis" );
}

QStringList QgsNearestNeighbourAnalysisAlgorithm::tags() const
{
  return QObject::tr( "point,node,vertex,nearest,neighbour,distance" ).split( ',' );
}

QString QgsNearestNeighbourAnalysisAlgorithm::group() const
{
  return QObject::tr( "Vector analysis" );
}

QString QgsNearestNeighbourAnalysisAlgorithm::groupId() const
{
  return QStringLiteral( "vectoranalysis" );
}

QString QgsNearestNeighbourAnalysisAlgorithm::shortHelpString() const
{
  return QObject::tr( "This algorithm performs nearest neighbor analysis for a point layer.\n\n"
                      "Output is generated as an HTML file with the computed statistical values." );
}

QString QgsNearestNeighbourAnalysisAlgorithm::svgIconPath() const
{
  return QgsApplication::iconPath( QStringLiteral( "/algorithms/mAlgorithmNearestNeighbour.svg" ) );
}

QIcon QgsNearestNeighbourAnalysisAlgorithm::icon() const
{
  return QgsApplication::getThemeIcon( QStringLiteral( "/algorithms/mAlgorithmNearestNeighbour.svg" ) );
}

QgsNearestNeighbourAnalysisAlgorithm *QgsNearestNeighbourAnalysisAlgorithm::createInstance() const
{
  return new QgsNearestNeighbourAnalysisAlgorithm();
}

void QgsNearestNeighbourAnalysisAlgorithm::initAlgorithm( const QVariantMap & )
{
  addParameter( new QgsProcessingParameterFeatureSource( QStringLiteral( "INPUT" ), QObject::tr( "Input layer" ), QList< int >() << QgsProcessing::TypeVectorPoint ) );
  addParameter( new QgsProcessingParameterFileDestination( QStringLiteral( "OUTPUT_HTML_FILE" ), QObject::tr( "Nearest neighbour" ),
                QObject::tr( "HTML files (*.html *.HTML)" ), QVariant(), true ) );
  addOutput( new QgsProcessingOutputNumber( QStringLiteral( "OBSERVED_MD" ), QObject::tr( "Observed mean distance" ) ) );
  addOutput( new QgsProcessingOutputNumber( QStringLiteral( "EXPECTED_MD" ), QObject::tr( "Expected mean distance" ) ) );
  addOutput( new QgsProcessingOutputNumber( QStringLiteral( "NN_INDEX" ), QObject::tr( "Nearest neighbour index" ) ) );
  addOutput( new QgsProcessingOutputNumber( QStringLiteral( "POINT_COUNT" ), QObject::tr( "Number of points" ) ) );
  addOutput( new QgsProcessingOutputNumber( QStringLiteral( "Z_SCORE" ), QObject::tr( "Z-score" ) ) );
}

QVariantMap QgsNearestNeighbourAnalysisAlgorithm::processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
  std::unique_ptr< QgsProcessingFeatureSource > source( parameterAsSource( parameters, QStringLiteral( "INPUT" ), context ) );
  if ( !source )
    throw QgsProcessingException( invalidSourceError( parameters, QStringLiteral( "INPUT" ) ) );

  QString outputFile = parameterAsFileOutput( parameters, QStringLiteral( "OUTPUT_HTML_FILE" ), context );

  QgsSpatialIndex spatialIndex( *source, feedback, QgsSpatialIndex::FlagStoreFeatureGeometries );
  QgsDistanceArea da;
  da.setSourceCrs( source->sourceCrs(), context.transformContext() );
  da.setEllipsoid( context.project()->ellipsoid() );

  double step = source->featureCount() ? 100.0 / source->featureCount() : 1;
  QgsFeatureIterator it = source->getFeatures( QgsFeatureRequest().setSubsetOfAttributes( QList< int >() ) );

  QgsFeatureRequest request;
  QgsFeature neighbour;
  double sumDist = 0.0;
  double area = source->sourceExtent().width() * source->sourceExtent().height();

  int i = 0;
  QgsFeature f;
  while ( it.nextFeature( f ) )
  {
    if ( feedback->isCanceled() )
    {
      break;
    }

    QgsFeatureId neighbourId = spatialIndex.nearestNeighbor( f.geometry().asPoint(), 2 ).at( 1 );
    sumDist += da.measureLine( spatialIndex.geometry( neighbourId ).asPoint(), f.geometry().asPoint() );

    i++;
    feedback->setProgress( i * step );
  }

  int count = source->featureCount() > 0 ? source->featureCount() : 1;
  double observedDistance = sumDist / count;
  double expectedDistance = 0.5 / std::sqrt( count / area );
  double nnIndex = observedDistance / expectedDistance;
  double se = 0.26136 / std::sqrt( std::pow( count, 2 ) / area );
  double zScore = ( observedDistance - expectedDistance ) / se;

  QVariantMap outputs;
  outputs.insert( QStringLiteral( "OBSERVED_MD" ), observedDistance );
  outputs.insert( QStringLiteral( "EXPECTED_MD" ), expectedDistance );
  outputs.insert( QStringLiteral( "NN_INDEX" ), nnIndex );
  outputs.insert( QStringLiteral( "POINT_COUNT" ), count );
  outputs.insert( QStringLiteral( "Z_SCORE" ), zScore );

  if ( !outputFile.isEmpty() )
  {
    QFile file( outputFile );
    if ( file.open( QIODevice::WriteOnly | QIODevice::Text ) )
    {
      QTextStream out( &file );
      out << QStringLiteral( "<html><head><meta http-equiv=\"Content-Type\" content=\"text/html;charset=utf-8\"/></head><body>\n" );
      out << QObject::tr( "<p>Observed mean distance: %1</p>\n" ).arg( observedDistance, 0, 'f', 11 );
      out << QObject::tr( "<p>Expected mean distance: %1</p>\n" ).arg( expectedDistance, 0, 'f', 11 );
      out << QObject::tr( "<p>Nearest neighbour index: %1</p>\n" ).arg( nnIndex, 0, 'f', 11 );
      out << QObject::tr( "<p>Number of points: %1</p>\n" ).arg( count );
      out << QObject::tr( "<p>Z-Score: %1</p>\n" ).arg( zScore, 0, 'f', 11 );
      out << QStringLiteral( "</body></html>" );

      outputs.insert( QStringLiteral( "OUTPUT_HTML_FILE" ), outputFile );
    }
  }

  return outputs;
}

///@endcond