/***************************************************************************
  qgsvectorlayerrenderer.cpp
  --------------------------------------
  Date                 : December 2013
  Copyright            : (C) 2013 by Martin Dobias
  Email                : wonder dot sk at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsvectorlayerrenderer.h"

#include "diagram/qgsdiagram.h"

#include "qgsdiagramrenderer.h"
#include "qgsmessagelog.h"
#include "qgspallabeling.h"
#include "qgsrenderer.h"
#include "qgsrendercontext.h"
#include "qgssinglesymbolrenderer.h"
#include "qgssymbollayer.h"
#include "qgssymbollayerutils.h"
#include "qgssymbol.h"
#include "qgsvectorlayer.h"
#include "qgsvectorlayerdiagramprovider.h"
#include "qgsvectorlayerfeatureiterator.h"
#include "qgsvectorlayerlabeling.h"
#include "qgsvectorlayerlabelprovider.h"
#include "qgspainteffect.h"
#include "qgsfeaturefilterprovider.h"
#include "qgsexception.h"
#include "qgslogger.h"
#include "qgssettings.h"
#include "qgsexpressioncontextutils.h"
#include "qgsrenderedfeaturehandlerinterface.h"
#include "qgsvectorlayertemporalproperties.h"

#include <QPicture>


QgsVectorLayerRenderer::QgsVectorLayerRenderer( QgsVectorLayer *layer, QgsRenderContext &context )
  : QgsMapLayerRenderer( layer->id(), &context )
  , mLayer( layer )
  , mFields( layer->fields() )
  , mLabeling( false )
  , mDiagrams( false )
{
  mSource = new QgsVectorLayerFeatureSource( layer );

  mRenderer = layer->renderer() ? layer->renderer()->clone() : nullptr;
  mSelectedFeatureIds = layer->selectedFeatureIds();

  mDrawVertexMarkers = nullptr != layer->editBuffer();

  mGeometryType = layer->geometryType();

  mFeatureBlendMode = layer->featureBlendMode();

  if ( context.isTemporal() )
  {
    QgsVectorLayerTemporalContext temporalContext;
    temporalContext.setLayer( layer );
    mTemporalFilter = qobject_cast< const QgsVectorLayerTemporalProperties * >( layer->temporalProperties() )->createFilterString( temporalContext, context.temporalRange() );
  }

  // if there's already a simplification method specified via the context, we respect that. Otherwise, we fall back
  // to the layer's individual setting
  if ( renderContext()->vectorSimplifyMethod().simplifyHints() != QgsVectorSimplifyMethod::NoSimplification )
  {
    mSimplifyMethod = renderContext()->vectorSimplifyMethod();
    mSimplifyGeometry = renderContext()->vectorSimplifyMethod().simplifyHints() & QgsVectorSimplifyMethod::GeometrySimplification ||
                        renderContext()->vectorSimplifyMethod().simplifyHints() & QgsVectorSimplifyMethod::FullSimplification;
  }
  else
  {
    mSimplifyMethod = layer->simplifyMethod();
    mSimplifyGeometry = layer->simplifyDrawingCanbeApplied( *renderContext(), QgsVectorSimplifyMethod::GeometrySimplification );
  }

  QgsSettings settings;
  mVertexMarkerOnlyForSelection = settings.value( QStringLiteral( "qgis/digitizing/marker_only_for_selected" ), true ).toBool();

  QString markerTypeString = settings.value( QStringLiteral( "qgis/digitizing/marker_style" ), "Cross" ).toString();
  if ( markerTypeString == QLatin1String( "Cross" ) )
  {
    mVertexMarkerStyle = QgsSymbolLayerUtils::Cross;
  }
  else if ( markerTypeString == QLatin1String( "SemiTransparentCircle" ) )
  {
    mVertexMarkerStyle = QgsSymbolLayerUtils::SemiTransparentCircle;
  }
  else
  {
    mVertexMarkerStyle = QgsSymbolLayerUtils::NoMarker;
  }

  mVertexMarkerSize = settings.value( QStringLiteral( "qgis/digitizing/marker_size_mm" ), 2.0 ).toDouble();

  if ( !mRenderer )
    return;

  QgsDebugMsgLevel( "rendering v2:\n  " + mRenderer->dump(), 2 );

  if ( mDrawVertexMarkers )
  {
    // set editing vertex markers style
    mRenderer->setVertexMarkerAppearance( mVertexMarkerStyle, mVertexMarkerSize );
  }
  renderContext()->expressionContext() << QgsExpressionContextUtils::layerScope( layer );

  mAttrNames = mRenderer->usedAttributes( context );
  if ( context.hasRenderedFeatureHandlers() )
  {
    const QList< QgsRenderedFeatureHandlerInterface * > handlers = context.renderedFeatureHandlers();
    for ( QgsRenderedFeatureHandlerInterface *handler : handlers )
      mAttrNames.unite( handler->usedAttributes( layer, context ) );
  }

  //register label and diagram layer to the labeling engine
  prepareLabeling( layer, mAttrNames );
  prepareDiagrams( layer, mAttrNames );
}


QgsVectorLayerRenderer::~QgsVectorLayerRenderer()
{
  delete mRenderer;
  delete mSource;
}

QgsFeedback *QgsVectorLayerRenderer::feedback() const
{
  return mInterruptionChecker.get();
}

bool QgsVectorLayerRenderer::render()
{

    qDebug() << "QgsVectorLayerRenderer::render()";

  if ( mGeometryType == QgsWkbTypes::NullGeometry || mGeometryType == QgsWkbTypes::UnknownGeometry )
    return true;

  if ( !mRenderer )
  {
    mErrors.append( QObject::tr( "No renderer for drawing." ) );
    return false;
  }

  if ( mRenderer->type() == QStringLiteral( "nullSymbol" ) )
  {
    // a little shortcut for the null symbol renderer - most of the time it is not going to render anything
    // so we can even skip the whole loop to fetch features
    if ( !mDrawVertexMarkers && !mLabelProvider && !mDiagramProvider && mSelectedFeatureIds.isEmpty() )
      return true;
  }

  qDebug() << "QgsVectorLayerRenderer ok";

  QgsRenderContext &context = *renderContext();

  // MUST be created in the thread doing the rendering
  mInterruptionChecker = qgis::make_unique< QgsVectorLayerRendererInterruptionChecker >( context );
  bool usingEffect = false;
  if ( mRenderer->paintEffect() && mRenderer->paintEffect()->enabled() )
  {
    usingEffect = true;
    mRenderer->paintEffect()->begin( context );
  }

  // Per feature blending mode
  if ( context.useAdvancedEffects() && mFeatureBlendMode != QPainter::CompositionMode_SourceOver )
  {
    // set the painter to the feature blend mode, so that features drawn
    // on this layer will interact and blend with each other
    context.painter()->setCompositionMode( mFeatureBlendMode );
  }

  mRenderer->startRender( context, mFields );

  QString rendererFilter = mRenderer->filter( mFields );

  QgsRectangle requestExtent = context.extent();
  mRenderer->modifyRequestExtent( requestExtent, context );

  QgsFeatureRequest featureRequest = QgsFeatureRequest()
                                     .setFilterRect( requestExtent )
                                     .setSubsetOfAttributes( mAttrNames, mFields )
                                     .setExpressionContext( context.expressionContext() );
  if ( mRenderer->orderByEnabled() )
  {
    featureRequest.setOrderBy( mRenderer->orderBy() );
  }

  const QgsFeatureFilterProvider *featureFilterProvider = context.featureFilterProvider();
  if ( featureFilterProvider )
  {
    featureFilterProvider->filterFeatures( mLayer, featureRequest );
  }
  if ( !rendererFilter.isEmpty() && rendererFilter != QLatin1String( "TRUE" ) )
  {
    featureRequest.combineFilterExpression( rendererFilter );
  }
  if ( !mTemporalFilter.isEmpty() )
  {
    featureRequest.combineFilterExpression( mTemporalFilter );
  }

  // enable the simplification of the geometries (Using the current map2pixel context) before send it to renderer engine.
  if ( mSimplifyGeometry )
  {
    double map2pixelTol = mSimplifyMethod.threshold();
    bool validTransform = true;

    const QgsMapToPixel &mtp = context.mapToPixel();
    map2pixelTol *= mtp.mapUnitsPerPixel();
    QgsCoordinateTransform ct = context.coordinateTransform();

    // resize the tolerance using the change of size of an 1-BBOX from the source CoordinateSystem to the target CoordinateSystem
    if ( ct.isValid() && !ct.isShortCircuited() )
    {
      try
      {
        QgsPointXY center = context.extent().center();
        double rectSize = ct.sourceCrs().isGeographic() ? 0.0008983 /* ~100/(40075014/360=111319.4833) */ : 100;

        QgsRectangle sourceRect = QgsRectangle( center.x(), center.y(), center.x() + rectSize, center.y() + rectSize );
        QgsRectangle targetRect = ct.transform( sourceRect );

        QgsDebugMsgLevel( QStringLiteral( "Simplify - SourceTransformRect=%1" ).arg( sourceRect.toString( 16 ) ), 4 );
        QgsDebugMsgLevel( QStringLiteral( "Simplify - TargetTransformRect=%1" ).arg( targetRect.toString( 16 ) ), 4 );

        if ( !sourceRect.isEmpty() && sourceRect.isFinite() && !targetRect.isEmpty() && targetRect.isFinite() )
        {
          QgsPointXY minimumSrcPoint( sourceRect.xMinimum(), sourceRect.yMinimum() );
          QgsPointXY maximumSrcPoint( sourceRect.xMaximum(), sourceRect.yMaximum() );
          QgsPointXY minimumDstPoint( targetRect.xMinimum(), targetRect.yMinimum() );
          QgsPointXY maximumDstPoint( targetRect.xMaximum(), targetRect.yMaximum() );

          double sourceHypothenuse = std::sqrt( minimumSrcPoint.sqrDist( maximumSrcPoint ) );
          double targetHypothenuse = std::sqrt( minimumDstPoint.sqrDist( maximumDstPoint ) );

          QgsDebugMsgLevel( QStringLiteral( "Simplify - SourceHypothenuse=%1" ).arg( sourceHypothenuse ), 4 );
          QgsDebugMsgLevel( QStringLiteral( "Simplify - TargetHypothenuse=%1" ).arg( targetHypothenuse ), 4 );

          if ( !qgsDoubleNear( targetHypothenuse, 0.0 ) )
            map2pixelTol *= ( sourceHypothenuse / targetHypothenuse );
        }
      }
      catch ( QgsCsException &cse )
      {
        QgsMessageLog::logMessage( QObject::tr( "Simplify transform error caught: %1" ).arg( cse.what() ), QObject::tr( "CRS" ) );
        validTransform = false;
      }
    }

    if ( validTransform )
    {
      QgsSimplifyMethod simplifyMethod;
      simplifyMethod.setMethodType( QgsSimplifyMethod::OptimizeForRendering );
      simplifyMethod.setTolerance( map2pixelTol );
      simplifyMethod.setThreshold( mSimplifyMethod.threshold() );
      simplifyMethod.setForceLocalOptimization( mSimplifyMethod.forceLocalOptimization() );
      featureRequest.setSimplifyMethod( simplifyMethod );

      QgsVectorSimplifyMethod vectorMethod = mSimplifyMethod;
      vectorMethod.setTolerance( map2pixelTol );
      context.setVectorSimplifyMethod( vectorMethod );
    }
    else
    {
      QgsVectorSimplifyMethod vectorMethod;
      vectorMethod.setSimplifyHints( QgsVectorSimplifyMethod::NoSimplification );
      context.setVectorSimplifyMethod( vectorMethod );
    }
  }
  else
  {
    QgsVectorSimplifyMethod vectorMethod;
    vectorMethod.setSimplifyHints( QgsVectorSimplifyMethod::NoSimplification );
    context.setVectorSimplifyMethod( vectorMethod );
  }

  QgsFeatureIterator fit = mSource->getFeatures( featureRequest );
  // Attach an interruption checker so that iterators that have potentially
  // slow fetchFeature() implementations, such as in the WFS provider, can
  // check it, instead of relying on just the mContext.renderingStopped() check
  // in drawRenderer()
  fit.setInterruptionChecker( mInterruptionChecker.get() );

  if ( ( mRenderer->capabilities() & QgsFeatureRenderer::SymbolLevels ) && mRenderer->usingSymbolLevels() )
    drawRendererLevels( fit );
  else
    drawRenderer( fit ); // DD drawRenderer

  if ( !fit.isValid() )
  {
    mErrors.append( QStringLiteral( "Data source invalid" ) );
  }

  if ( usingEffect )
  {
    mRenderer->paintEffect()->end( context );
  }

  mInterruptionChecker.reset();
  return true;
}


void QgsVectorLayerRenderer::drawRenderer( QgsFeatureIterator &fit )
{
    qDebug() << "QgsVectorLayerRenderer::drawRenderer";
  QgsExpressionContextScope *symbolScope = QgsExpressionContextUtils::updateSymbolScope( nullptr, new QgsExpressionContextScope() );
  QgsRenderContext &context = *renderContext();
  context.expressionContext().appendScope( symbolScope );

  QgsFeature fet;
  while ( fit.nextFeature( fet ) )
  {
    try
    {
      if ( context.renderingStopped() )
      {
        QgsDebugMsgLevel( QStringLiteral( "Drawing of vector layer %1 canceled." ).arg( layerId() ), 2 );
        break;
      }

      if ( !fet.hasGeometry() || fet.geometry().isEmpty() )
        continue; // skip features without geometry

      context.expressionContext().setFeature( fet );

      bool sel = context.showSelection() && mSelectedFeatureIds.contains( fet.id() );
      bool drawMarker = ( mDrawVertexMarkers && context.drawEditingInformation() && ( !mVertexMarkerOnlyForSelection || sel ) );

      // render feature
      bool rendered = mRenderer->renderFeature( fet, context, -1, sel, drawMarker );

      static int i = 0;
      qDebug() << "rendered:" << rendered << ++i;

      // labeling - register feature
      if ( rendered )
      {
        // new labeling engine
        if ( context.labelingEngine() && ( mLabelProvider || mDiagramProvider ) )
        {
          QgsGeometry obstacleGeometry;
          QgsSymbolList symbols = mRenderer->originalSymbolsForFeature( fet, context );
          QgsSymbol *symbol = nullptr;
          if ( !symbols.isEmpty() && fet.geometry().type() == QgsWkbTypes::PointGeometry )
          {
            obstacleGeometry = QgsVectorLayerLabelProvider::getPointObstacleGeometry( fet, context, symbols );
          }

          if ( !symbols.isEmpty() )
          {
            symbol = symbols.at( 0 );
            QgsExpressionContextUtils::updateSymbolScope( symbol, symbolScope );
          }

          if ( mLabelProvider )
          {
            mLabelProvider->registerFeature( fet, context, obstacleGeometry, symbol );
          }
          if ( mDiagramProvider )
          {
            mDiagramProvider->registerFeature( fet, context, obstacleGeometry );
          }
        }
      }
    }
    catch ( const QgsCsException &cse )
    {
      Q_UNUSED( cse )
      QgsDebugMsg( QStringLiteral( "Failed to transform a point while drawing a feature with ID '%1'. Ignoring this feature. %2" )
                   .arg( fet.id() ).arg( cse.what() ) );
    }
  }

  delete context.expressionContext().popScope();

  stopRenderer( nullptr );
}

void QgsVectorLayerRenderer::drawRendererLevels( QgsFeatureIterator &fit )
{
    qDebug() << "QgsVectorLayerRenderer::drawRendererLevels";
  QHash< QgsSymbol *, QList<QgsFeature> > features; // key = symbol, value = array of features
  QgsRenderContext &context = *renderContext();

  QgsSingleSymbolRenderer *selRenderer = nullptr;
  if ( !mSelectedFeatureIds.isEmpty() )
  {
    selRenderer = new QgsSingleSymbolRenderer( QgsSymbol::defaultSymbol( mGeometryType ) );
    selRenderer->symbol()->setColor( context.selectionColor() );
    selRenderer->setVertexMarkerAppearance( mVertexMarkerStyle, mVertexMarkerSize );
    selRenderer->startRender( context, mFields );
  }

  QgsExpressionContextScope *symbolScope = QgsExpressionContextUtils::updateSymbolScope( nullptr, new QgsExpressionContextScope() );
  std::unique_ptr< QgsExpressionContextScopePopper > scopePopper = qgis::make_unique< QgsExpressionContextScopePopper >( context.expressionContext(), symbolScope );

  // 1. fetch features
  QgsFeature fet;
  while ( fit.nextFeature( fet ) )
  {
    if ( context.renderingStopped() )
    {
      qDebug( "rendering stop!" );
      stopRenderer( selRenderer );
      return;
    }

    if ( !fet.hasGeometry() )
      continue; // skip features without geometry

    context.expressionContext().setFeature( fet );
    QgsSymbol *sym = mRenderer->symbolForFeature( fet, context );
    if ( !sym )
    {
      continue;
    }

    if ( !features.contains( sym ) )
    {
      features.insert( sym, QList<QgsFeature>() );
    }
    features[sym].append( fet );

    // new labeling engine
    if ( context.labelingEngine() && ( mLabelProvider || mDiagramProvider ) )
    {
      QgsGeometry obstacleGeometry;
      QgsSymbolList symbols = mRenderer->originalSymbolsForFeature( fet, context );
      QgsSymbol *symbol = nullptr;
      if ( !symbols.isEmpty() && fet.geometry().type() == QgsWkbTypes::PointGeometry )
      {
        obstacleGeometry = QgsVectorLayerLabelProvider::getPointObstacleGeometry( fet, context, symbols );
      }

      if ( !symbols.isEmpty() )
      {
        symbol = symbols.at( 0 );
        QgsExpressionContextUtils::updateSymbolScope( symbol, symbolScope );
      }

      if ( mLabelProvider )
      {
        mLabelProvider->registerFeature( fet, context, obstacleGeometry, symbol );
      }
      if ( mDiagramProvider )
      {
        mDiagramProvider->registerFeature( fet, context, obstacleGeometry );
      }
    }
  }

  scopePopper.reset();

  if ( features.empty() )
  {
    // nothing to draw
    stopRenderer( selRenderer );
    return;
  }

  // find out the order
  QgsSymbolLevelOrder levels;
  QgsSymbolList symbols = mRenderer->symbols( context );
  for ( int i = 0; i < symbols.count(); i++ )
  {
    QgsSymbol *sym = symbols[i];
    for ( int j = 0; j < sym->symbolLayerCount(); j++ )
    {
      int level = sym->symbolLayer( j )->renderingPass();
      if ( level < 0 || level >= 1000 ) // ignore invalid levels
        continue;
      QgsSymbolLevelItem item( sym, j );
      while ( level >= levels.count() ) // append new empty levels
        levels.append( QgsSymbolLevel() );
      levels[level].append( item );
    }
  }

  // 2. draw features in correct order
  for ( int l = 0; l < levels.count(); l++ )
  {
    QgsSymbolLevel &level = levels[l];
    for ( int i = 0; i < level.count(); i++ )
    {
      QgsSymbolLevelItem &item = level[i];
      if ( !features.contains( item.symbol() ) )
      {
        QgsDebugMsg( QStringLiteral( "level item's symbol not found!" ) );
        continue;
      }
      int layer = item.layer();
      QList<QgsFeature> &lst = features[item.symbol()];
      QList<QgsFeature>::iterator fit;
      for ( fit = lst.begin(); fit != lst.end(); ++fit )
      {
        if ( context.renderingStopped() )
        {
          stopRenderer( selRenderer );
          return;
        }

        bool sel = context.showSelection() && mSelectedFeatureIds.contains( fit->id() );
        // maybe vertex markers should be drawn only during the last pass...
        bool drawMarker = ( mDrawVertexMarkers && context.drawEditingInformation() && ( !mVertexMarkerOnlyForSelection || sel ) );

        context.expressionContext().setFeature( *fit );

        try
        {
          mRenderer->renderFeature( *fit, context, layer, sel, drawMarker );
        }
        catch ( const QgsCsException &cse )
        {
          Q_UNUSED( cse )
          QgsDebugMsg( QStringLiteral( "Failed to transform a point while drawing a feature with ID '%1'. Ignoring this feature. %2" )
                       .arg( fet.id() ).arg( cse.what() ) );
        }
      }
    }
  }

  stopRenderer( selRenderer );
}


void QgsVectorLayerRenderer::stopRenderer( QgsSingleSymbolRenderer *selRenderer )
{
  QgsRenderContext &context = *renderContext();
  mRenderer->stopRender( context );
  if ( selRenderer )
  {
    selRenderer->stopRender( context );
    delete selRenderer;
  }
}




void QgsVectorLayerRenderer::prepareLabeling( QgsVectorLayer *layer, QSet<QString> &attributeNames )
{
  QgsRenderContext &context = *renderContext();
  // TODO: add attributes for geometry generator
  if ( QgsLabelingEngine *engine2 = context.labelingEngine() )
  {
    if ( layer->labelsEnabled() )
    {
      mLabelProvider = layer->labeling()->provider( layer );
      if ( mLabelProvider )
      {
        engine2->addProvider( mLabelProvider );
        if ( !mLabelProvider->prepare( context, attributeNames ) )
        {
          engine2->removeProvider( mLabelProvider );
          mLabelProvider = nullptr; // deleted by engine
        }
      }
    }
  }

#if 0 // TODO: limit of labels, font not found
  QgsPalLayerSettings &palyr = mContext.labelingEngine()->layer( mLayerID );

  // see if feature count limit is set for labeling
  if ( palyr.limitNumLabels && palyr.maxNumLabels > 0 )
  {
    QgsFeatureIterator fit = getFeatures( QgsFeatureRequest()
                                          .setFilterRect( mContext.extent() )
                                          .setNoAttributes() );

    // total number of features that may be labeled
    QgsFeature f;
    int nFeatsToLabel = 0;
    while ( fit.nextFeature( f ) )
    {
      nFeatsToLabel++;
    }
    palyr.mFeaturesToLabel = nFeatsToLabel;
  }

  // notify user about any font substitution
  if ( !palyr.mTextFontFound && !mLabelFontNotFoundNotified )
  {
    emit labelingFontNotFound( this, palyr.mTextFontFamily );
    mLabelFontNotFoundNotified = true;
  }
#endif
}

void QgsVectorLayerRenderer::prepareDiagrams( QgsVectorLayer *layer, QSet<QString> &attributeNames )
{
  QgsRenderContext &context = *renderContext();
  if ( QgsLabelingEngine *engine2 = context.labelingEngine() )
  {
    if ( layer->diagramsEnabled() )
    {
      mDiagramProvider = new QgsVectorLayerDiagramProvider( layer );
      // need to be added before calling prepare() - uses map settings from engine
      engine2->addProvider( mDiagramProvider );
      if ( !mDiagramProvider->prepare( context, attributeNames ) )
      {
        engine2->removeProvider( mDiagramProvider );
        mDiagramProvider = nullptr;  // deleted by engine
      }
    }
  }
}

/*  -----------------------------------------  */
/*  QgsVectorLayerRendererInterruptionChecker  */
/*  -----------------------------------------  */

QgsVectorLayerRendererInterruptionChecker::QgsVectorLayerRendererInterruptionChecker
( const QgsRenderContext &context )
  : mContext( context )
  , mTimer( new QTimer( this ) )
{
  connect( mTimer, &QTimer::timeout, this, [ = ]
  {
    if ( mContext.renderingStopped() )
    {
      mTimer->stop();
      cancel();
    }
  } );
  mTimer->start( 50 );

}
