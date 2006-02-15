/*! 
* \file forest_production_technology.cpp
* \ingroup CIAM
* \brief ForestProductionTechnology class source file.
* \author James Blackwood
* \date $Date$
* \version $Revision$
*/

#include "technologies/include/forest_production_technology.h"
#include "land_allocator/include/iland_allocator.h"
#include "emissions/include/ghg.h"
#include "containers/include/scenario.h"
#include "util/base/include/xml_helper.h"
#include "containers/include/iinfo.h"
#include "marketplace/include/marketplace.h"

using namespace std;
using namespace xercesc;

extern Scenario* scenario;
// static initialize.
const string ForestProductionTechnology::prefix = "Future";

// Technology class method definition

//! Constructor.
ForestProductionTechnology::ForestProductionTechnology(){
    // TODO: 0.02 should not be a default value.
    interestRate = 0.02;
    forestLandAside = 0;
    rotationPeriod = 0;
    futureProduction = -1;
}

// ! Destructor
ForestProductionTechnology::~ForestProductionTechnology() {
}

//! Parses any input variables specific to derived classes
bool ForestProductionTechnology::XMLDerivedClassParse( const string& nodeName, const DOMNode* curr ) {
    const Modeltime* modeltime = scenario->getModeltime();
    
    if( nodeName == "interestRate" ) {
        interestRate = XMLHelper<int>::getValue( curr );
    }
    else if( nodeName == "futureProduction" ) {
        futureProduction = XMLHelper<double>::getValue( curr );
    }
    else if( !FoodProductionTechnology::XMLDerivedClassParse(nodeName, curr)) {
        return false;
    }
    return true;
}

//! write object to xml output stream
void ForestProductionTechnology::toInputXMLDerived( ostream& out, Tabs* tabs ) const {
    FoodProductionTechnology::toInputXMLDerived( out, tabs);
    XMLWriteElementCheckDefault( futureProduction, "futureProduction", out, tabs, -1.0 );
    XMLWriteElementCheckDefault( interestRate, "interestRate", out, tabs, 0.02 );
}

//! write object to xml output stream
void ForestProductionTechnology::toDebugXMLDerived( const int period, ostream& out, Tabs* tabs ) const {
    FoodProductionTechnology::toDebugXMLDerived( period, out, tabs);
    XMLWriteElement( futureProduction, "futureProduction", out, tabs );
    XMLWriteElement( interestRate, "interestRate", out, tabs );
}

/*! \brief Get the XML node name for output to XML.
*
* This public function accesses the private constant string, XML_NAME.
* This way the tag is always consistent for both read-in and output and can be easily changed.
* This function may be virtual to be overriden by derived class pointers.
* \author Josh Lurz, James Blackwood
* \return The constant XML_NAME.
*/
const std::string& ForestProductionTechnology::getXMLName1D() const {
    return getXMLNameStatic1D();
}

/*! \brief Get the XML node name in static form for comparison when parsing XML.
*
* This public function accesses the private constant string, XML_NAME.
* This way the tag is always consistent for both read-in and output and can be easily changed.
* The "==" operator that is used when parsing, required this second function to return static.
* \note A function cannot be static and virtual.
* \author Josh Lurz, James Blackwood
* \return The constant XML_NAME as a static.
*/
const std::string& ForestProductionTechnology::getXMLNameStatic1D() {
    const static string XML_NAME = "ForestProductionTechnology";
    return XML_NAME;
}

//! Clone Function. Returns a deep copy of the current technology.
ForestProductionTechnology* ForestProductionTechnology::clone() const {
    return new ForestProductionTechnology( *this );
}

/*! 
* \brief Perform initializations that only need to be done once per period.
* \param aRegionName Region name.
* \param aSectorName Sector name, also the name of the product.
* \param aSubsectorInfo Parent information container.
* \param aDemographics Regional demographics container.
* \param aPeriod Model period.
*/
void ForestProductionTechnology::initCalc( const string& aRegionName,
                                           const string& aSectorName,
                                           const IInfo* aSubsectorInfo,
                                           const Demographic* aDemographics,
                                           const int aPeriod )
{
    const Modeltime* modeltime = scenario->getModeltime();

    // Only apply productivity change after 1990 since 1990 is calibrated year
    // At present, however, it is necessary that the 1990 productivity change is
    // equal to that for years between 1990 and the rotation period
    if ( year > 1990 ) {
        mLandAllocator->applyAgProdChange( landType, name, agProdChange, modeltime->getyr_to_per( year ) );
    }

    // Set calibrated values to land allocator in case these were disrupted in previous period
    setCalLandValues();

    Marketplace* marketplace = scenario->getMarketplace();
    // TODO - The yield here should probably be the future yield, not the current year calibrationed yield.
    if (( calObservedYield != -1 ) && ( year != modeltime->getEndYear() )){
        double calPrice = marketplace->getMarketInfo( name, aRegionName, modeltime->getyr_to_per( year ), true )->getDouble( "calPrice", true );
        double profitFactor = mLandAllocator->getCalAveObservedRate( "UnmanagedLand", modeltime->getyr_to_per( year ) ) / calcDiscountFactor();
        double calVarCost = calPrice - profitFactor / calObservedYield;
        if ( calVarCost > 0 ) {
            variableCost = calVarCost;
            marketplace->getMarketInfo( name, aRegionName, ( modeltime->getyr_to_per( year ) + 1 ), true )->setDouble( "calVarCost", calVarCost );
        }
        else {
            ILogger& mainLog = ILogger::getLogger( "main_log" );
            mainLog.setLevel( ILogger::DEBUG );
            mainLog << "Read in value for calPrice in " << aRegionName << " " << name << " is too low by:" << 0 - calVarCost << endl;
            marketplace->getMarketInfo( name, aRegionName, ( modeltime->getyr_to_per( year ) + 1 ), true )->setDouble( "calVarCost", calVarCost );
        }

        if ( calProduction != -1 ) {
            // Set calibration information to marketplace for reporting purposes
            IInfo* marketInfo = marketplace->getMarketInfo( aSectorName, aRegionName, modeltime->getyr_to_per( year ) , true );

            // Also set value to marketplace for future forest demand if there are no price effects
            marketInfo = marketplace->getMarketInfo( prefix + aSectorName, aRegionName, modeltime->getyr_to_per( year ) , true );
            double existingDemand = max( marketInfo->getDouble( "calSupply", false ), 0.0 );
            marketInfo->setDouble( "calSupply", existingDemand + futureProduction );
        }
    }
    else {
        double calVarCost = marketplace->getMarketInfo( name, aRegionName, ( modeltime->getyr_to_per( year ) ), true )->getDouble( "calVarCost", false );
        if ( year  != modeltime->getEndYear( ) ) {
            marketplace->getMarketInfo( name, aRegionName, ( modeltime->getyr_to_per( year ) + 1 ), true )->setDouble( "calVarCost", calVarCost );
        }
        if ( calVarCost > 0 ) {
            variableCost = calVarCost;
        }
    }

    technology::initCalc( aRegionName, aSectorName, aSubsectorInfo,
                          aDemographics, aPeriod );
}

/*!
* \brief Complete the initialization of the technology.
* \note This routine is only called once per model run
* \param aSectorName Sector name, also the name of the product.
* \param aDepDefinder Regional dependency finder.
* \param aSubsectorInfo Subsector information object.
* \param aLandAllocator Regional land allocator.
* \author Josh Lurz
* \warning Markets are not necesarilly set when completeInit is called
* \author James Blackwood
* \warning This may break if timestep is not constant for each time period.
*/
void ForestProductionTechnology::completeInit( const string& aSectorName,
                                              DependencyFinder* aDepFinder,
                                              const IInfo* aSubsectorInfo,
                                              ILandAllocator* aLandAllocator )
{
    // Store away the land allocator.
    mLandAllocator = aLandAllocator;

    // Set rotation period variable so this can be used throughout object
    rotationPeriod = aSubsectorInfo->getInteger( "rotationPeriod", true );

    // Setup the land usage for this production. Only add land usage once for
    // all technologies, of a given type. TODO: This is error prone if
    // technologies don't all have the same land type.
    if( year == scenario->getModeltime()->getStartYear() ){
        mLandAllocator->addLandUsage( landType, name, ILandAllocator::eForest );
    }

    setCalLandValues();

    technology::completeInit( aSectorName, aDepFinder, aSubsectorInfo,
        aLandAllocator );
}

/*! \brief Sets calibrated land values to land allocator.
*
* This utility function is called twice. Once in completeInit so that initial
* shares can be set throughout the land allocator and again in initCalc()
* in case shares have been disrupted by a previous call to calc() (which is what
* currently happens in 1975).
*
* \author Steve Smith
*/
void ForestProductionTechnology::setCalLandValues() {
    const Modeltime* modeltime = scenario->getModeltime();
    int timestep = modeltime->gettimestep( modeltime->getyr_to_per(year));
    int nRotPeriodSteps = rotationPeriod / timestep;

    // -1 means not read in
    if (( calProduction != -1 ) && ( calYield != -1 )) {
        calObservedYield = 0;     //Yield per year
        double calObservedLandUsed = 0;  //Land harvested per period as opposed to per step-periods in calLandUsed
        double calProductionTemp = calProduction;
        double calYieldTemp = calYield;
        int period = modeltime->getyr_to_per(year);
        if ( futureProduction == -1 ) {
            nRotPeriodSteps = 0;
        }

        for ( int i = period; i <= period + nRotPeriodSteps; i++ ) {
            // Need to do be able to somehow get productivity change from other
            // periods. Or demand that productivity change is the same for all
            // calibration periods (could test in applyAgProdChange)
            if ( i > period ) {
                calProductionTemp += ( futureProduction - calProduction ) / nRotPeriodSteps;
                calYieldTemp = calYield * pow( 1 + agProdChange, double( timestep * ( i - 1 ) ) );
            }

            calLandUsed = calProductionTemp / calYieldTemp;
            mLandAllocator->setCalLandAllocation( landType, name, calLandUsed, i, period );
            mLandAllocator->setCalObservedYield( landType, name, calYieldTemp, i );
            if ( i == period ) {
                calObservedYield = calYieldTemp;
            }
        }      
    }
}

/*!
* \brief Calculate unnormalized technology unnormalized shares.
* \details Since food and forestry technolgies are profit based, they do not
*          directly calculate a share. Instead, their share of total supply is
*          determined by the sharing which occurs in the land allocator. To
*          facilitate this the technology sets the intrinisic rate for the land
*          use into the land allocator. The technology share itself is set to 1.
* \param aRegionName Region name.
* \param aSectorName Sector name, also the name of the product.
* \param aGDP Regional GDP container.
* \param aPeriod Model period.
* \author James Blackwood, Steve Smith
*/
void ForestProductionTechnology::calcShare( const string& aRegionName,
                                            const string& aSectorName,
                                            const GDP* aGDP,
                                            const int aPeriod )
{
    double profitRate = calcProfitRate( aRegionName, getFutureMarket( aSectorName ), aPeriod );

    profitRate = max( profitRate, 0.0 );

    mLandAllocator->setIntrinsicRate( aRegionName, landType, name, profitRate, aPeriod );
    
    // Forest production technologies are profit based, so the amount of output
    // they produce is independent of the share.
    share = 1;
}

/*! \brief Calculates the output of the technology.
* \details Calculates the amount of current forestry output based on the amount
*          of planted forestry land and it's yield. Forestry production
*          technologies are profit based and determine their supply
*          independently of the passed in subsector demand. However, since this
*          is a solved market, in equalibrium the sum of the production of
*          technologies within a sector will equal the demand for the sector.
*          For forestry this supply is fixed because trees were planted several
*          periods before. Since the supply is inelastic, demand must adjust to
*          reach equalibrium.
* \param aRegionName Region name.
* \param aSectorName Sector name, also the name of the product.
* \param aDemand Subsector demand for output.
* \param aGDP Regional GDP container.
* \param aPeriod Model period.
*/
void ForestProductionTechnology::production( const string& aRegionName,
                                             const string& aSectorName,
                                             const double aDemand,
                                             const GDP* aGDP,
                                             const int aPeriod )
{
    // Calculate profit rate.
    double profitRate = calcProfitRate( aRegionName, getFutureMarket( aSectorName ), aPeriod );

    // Calculating the yield for future forest.
    const int harvestPeriod = getHarvestPeriod( aPeriod );
    mLandAllocator->calcYield( landType, name, profitRate, harvestPeriod, aPeriod );
    
    // Add the supply of future forestry to the future market.
    double futureSupply = calcSupply( aRegionName, aSectorName, harvestPeriod );
    Marketplace* marketplace = scenario->getMarketplace();
    marketplace->addToSupply( getFutureMarket( aSectorName ), aRegionName, futureSupply, aPeriod );

    // now calculate the amount to be consumed this period (ie. planted steps
    // periods ago).
    output = calcSupply( aRegionName, aSectorName, aPeriod );
    marketplace->addToSupply( name, aRegionName, output, aPeriod );

    // Set the input to be the land used.
    input = mLandAllocator->getLandAllocation( aSectorName, aPeriod );

    // calculate emissions for each gas after setting input and output amounts
    for ( unsigned int i = 0; i < ghg.size(); ++i ) {
        ghg[ i ]->calcEmission( aRegionName, fuelname, input, aSectorName, output, aGDP, aPeriod );
    }
}

/*! \brief Calculate the profit rate for the technology.
* \details Calculates the profit rate for the forestry technology. This is equal
*          to the net present value of the market price minus the variable cost 
*          Profit rate can be negative.
* \param aRegionName Region name.
* \param aProductName Name of the product for which to calculate the profit
*        rate. Must be an output of the technology.
* \return The profit rate.
*/
double ForestProductionTechnology::calcProfitRate( const string& aRegionName,
                                                   const string& aProductName,
                                                   const int aPeriod ) const
{
    // Calculate the future profit rate.
    // TODO: If a ForestProductionTechnology had emissions this would not be correct as the 
    // emissions cost would be calculated for the present year and the emissions would be 
    // charged in a future year.
    double profitRate = FoodProductionTechnology::calcProfitRate( aRegionName, aProductName, aPeriod );

    // Calculate the net present value.
    double netPresentValue = profitRate * calcDiscountFactor();

    return netPresentValue;
}

/*! \brief Calculate the factor to discount between the present period and the harvest period.
* \return The discount factor.
*/
double ForestProductionTechnology::calcDiscountFactor() const {
    return interestRate / ( pow( 1 + interestRate, rotationPeriod ) - 1 );
}

/*! \brief Get the period in which the crop will be harvested if planted in the
*          current period.
* \param aCurrentPeriod Current period.
* \return The harvest period.
*/
int ForestProductionTechnology::getHarvestPeriod( const int aCurrentPeriod ) const {
    const Modeltime* modeltime = scenario->getModeltime();
    return aCurrentPeriod + rotationPeriod / modeltime->gettimestep( modeltime->getyr_to_per( year ) );
}

/*! \brief Get the future market for a given product name.
* \param aProductName Name of the product.
* \return Name of the future market.
*/
const string ForestProductionTechnology::getFutureMarket( const string& aProductName ) const {
    return prefix + aProductName;
}