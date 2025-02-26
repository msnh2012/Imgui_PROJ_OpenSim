#include "TPS3D.hpp"

#include <oscar/Bindings/GlmHelpers.hpp>
#include <oscar/Formats/CSV.hpp>
#include <oscar/Utils/Algorithms.hpp>
#include <oscar/Utils/Perf.hpp>

#include <Simbody.h>

#include <fstream>
#include <iostream>

namespace
{
    // this is effectviely the "U" term in the TPS algorithm literature
    //
    // i.e. U(||pi - p||) in the literature is equivalent to `RadialBasisFunction3D(pi, p)` here
    float RadialBasisFunction3D(glm::vec3 const& controlPoint, glm::vec3 const& p)
    {
        // this implementation uses the U definition from the following (later) source:
        //
        // Chapter 3, "Semilandmarks in Three Dimensions" by Phillip Gunz, Phillip Mitteroecker,
        // and Fred L. Bookstein
        //
        // the original Bookstein paper uses U(v) = |v|^2 * log(|v|^2), but subsequent literature
        // (e.g. the above book) uses U(v) = |v|. The primary author (Gunz) claims that the original
        // basis function is not as good as just using the magnitude?

        return glm::length(controlPoint - p);
    }
}

bool osc::operator==(LandmarkPair3D const& a, LandmarkPair3D const& b) noexcept
{
    return a.source == b.source && a.destination == b.destination;
}

bool osc::operator!=(LandmarkPair3D const& a, LandmarkPair3D const& b) noexcept
{
    return !(a == b);
}

std::ostream& osc::operator<<(std::ostream& o, LandmarkPair3D const& p)
{
    using osc::operator<<;
    o << "LandmarkPair3D{Src = " << p.source << ", dest = " << p.destination << '}';
    return o;
}

bool osc::operator==(TPSCoefficientSolverInputs3D const& a, TPSCoefficientSolverInputs3D const& b) noexcept
{
    return a.landmarks == b.landmarks && a.blendingFactor == b.blendingFactor;
}

bool osc::operator!=(TPSCoefficientSolverInputs3D const& a, TPSCoefficientSolverInputs3D const& b) noexcept
{
    return !(a == b);
}

std::ostream& osc::operator<<(std::ostream& o, TPSCoefficientSolverInputs3D const& inputs)
{
    o << "TPSCoefficientSolverInputs3D{landmarks = [";
    std::string_view delim = "";
    for (LandmarkPair3D const& landmark : inputs.landmarks)
    {
        o << delim << landmark;
        delim = ", ";
    }
    o << "], BlendingFactor = " << inputs.blendingFactor << '}';
    return o;
}

bool osc::operator==(TPSNonAffineTerm3D const& a, TPSNonAffineTerm3D const& b) noexcept
{
    return a.weight == b.weight && a.controlPoint == b.controlPoint;
}

bool osc::operator!=(TPSNonAffineTerm3D const& a, TPSNonAffineTerm3D const& b) noexcept
{
    return !(a == b);
}

std::ostream& osc::operator<<(std::ostream& o, TPSNonAffineTerm3D const& wt)
{
    using osc::operator<<;
    return o << "TPSNonAffineTerm3D{Weight = " << wt.weight << ", ControlPoint = " << wt.controlPoint << '}';
}

bool osc::operator==(TPSCoefficients3D const& a, TPSCoefficients3D const& b) noexcept
{
    return
        a.a1 == b.a1 &&
        a.a2 == b.a2 &&
        a.a3 == b.a3 &&
        a.a4 == b.a4 &&
        a.nonAffineTerms == b.nonAffineTerms;
}

bool osc::operator!=(TPSCoefficients3D const& a, TPSCoefficients3D const& b) noexcept
{
    return !(a == b);
}

std::ostream& osc::operator<<(std::ostream& o, TPSCoefficients3D const& coefs)
{
    using osc::operator<<;
    o << "TPSCoefficients3D{a1 = " << coefs.a1 << ", a2 = " << coefs.a2 << ", a3 = " << coefs.a3 << ", a4 = " << coefs.a4;
    for (size_t i = 0; i < coefs.nonAffineTerms.size(); ++i)
    {
        o << ", w" << i << " = " << coefs.nonAffineTerms[i];
    }
    o << '}';
    return o;
}

// computes all coefficients of the 3D TPS equation (a1, a2, a3, a4, and all the w's)
osc::TPSCoefficients3D osc::CalcCoefficients(TPSCoefficientSolverInputs3D const& inputs)
{
    // this is based on the Bookstein Thin Plate Sline (TPS) warping algorithm
    //
    // 1. A TPS warp is (simplifying here) a linear combination:
    //
    //     f(p) = a1 + a2*p.x + a3*p.y + a4*p.z + SUM{ wi * U(||controlPoint_i - p||) }
    //
    //    which can be represented as a matrix multiplication between the terms (1, p.x, p.y,
    //    p.z, U(||cpi - p||)) and the coefficients (a1, a2, a3, a4, wi..)
    //
    // 2. The caller provides "landmark pairs": these are (effectively) the input
    //    arguments and the expected output
    //
    // 3. This algorithm uses the input + output to solve for the linear coefficients.
    //    Once those coefficients are known, we then have a linear equation that we
    //    we can pump new inputs into (e.g. mesh points, muscle points)
    //
    // 4. So, given the equation L * [w a] = [v o], where L is a matrix of linear terms,
    //    [w a] is a vector of the linear coefficients (we're solving for these), and [v o]
    //    is the expected output (v), with some (padding) zero elements (o)
    //
    // 5. Create matrix L:
    //
    //   |K  P|
    //   |PT 0|
    //
    //     where:
    //
    //     - K is a symmetric matrix of each *input* landmark pair evaluated via the
    //       basis function:
    //
    //        |U(p00) U(p01) U(p02)  ...  |
    //        |U(p10) U(p11) U(p12)  ...  |
    //        | ...    ...    ...   U(pnn)|
    //
    //     - P is a n-row 4-column matrix containing the number 1 (the constant term),
    //       x, y, and z (effectively, the p term):
    //
    //       |1 x1 y1 z1|
    //       |1 x2 y2 z2|
    //
    //     - PT is the transpose of P
    //     - 0 is a 4x4 zero matrix (padding)
    //
    // 6. Use a linear solver to solve L * [w a] = [v o] to yield [w a]
    // 8. Return the coefficients, [w a]

    OSC_PERF("CalcCoefficients");

    int const numPairs = static_cast<int>(inputs.landmarks.size());

    if (numPairs == 0)
    {
        // edge-case: there are no pairs, so return an identity-like transform
        return TPSCoefficients3D{};
    }

    // construct matrix L
    SimTK::Matrix L(numPairs + 4, numPairs + 4);

    // populate the K part of matrix L (upper-left)
    for (int row = 0; row < numPairs; ++row)
    {
        for (int col = 0; col < numPairs; ++col)
        {
            glm::vec3 const& pi = inputs.landmarks[row].source;
            glm::vec3 const& pj = inputs.landmarks[col].source;

            L(row, col) = RadialBasisFunction3D(pi, pj);
        }
    }

    // populate the P part of matrix L (upper-right)
    {
        int const pStartColumn = numPairs;

        for (int row = 0; row < numPairs; ++row)
        {
            L(row, pStartColumn)     = 1.0;
            L(row, pStartColumn + 1) = inputs.landmarks[row].source.x;
            L(row, pStartColumn + 2) = inputs.landmarks[row].source.y;
            L(row, pStartColumn + 3) = inputs.landmarks[row].source.z;
        }
    }

    // populate the PT part of matrix L (bottom-left)
    {
        int const ptStartRow = numPairs;

        for (int col = 0; col < numPairs; ++col)
        {
            L(ptStartRow, col)     = 1.0;
            L(ptStartRow + 1, col) = inputs.landmarks[col].source.x;
            L(ptStartRow + 2, col) = inputs.landmarks[col].source.y;
            L(ptStartRow + 3, col) = inputs.landmarks[col].source.z;
        }
    }

    // populate the 0 part of matrix L (bottom-right)
    {
        int const zeroStartRow = numPairs;
        int const zeroStartCol = numPairs;

        for (int row = 0; row < 4; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                L(zeroStartRow + row, zeroStartCol + col) = 0.0;
            }
        }
    }

    // construct "result" vectors Vx and Vy (these hold the landmark destinations)
    SimTK::Vector Vx(numPairs + 4, 0.0);
    SimTK::Vector Vy(numPairs + 4, 0.0);
    SimTK::Vector Vz(numPairs + 4, 0.0);
    for (int row = 0; row < numPairs; ++row)
    {
        glm::vec3 const blended = glm::mix(inputs.landmarks[row].source, inputs.landmarks[row].destination, inputs.blendingFactor);
        Vx[row] = blended.x;
        Vy[row] = blended.y;
        Vz[row] = blended.z;
    }

    // create a linear solver that can be used to solve `L*Cn = Vn` for `Cn` (where `n` is a dimension)
    SimTK::FactorQTZ const F{L};

    // solve for each dimension
    SimTK::Vector Cx(numPairs + 4, 0.0);
    F.solve(Vx, Cx);
    SimTK::Vector Cy(numPairs + 4, 0.0);
    F.solve(Vy, Cy);
    SimTK::Vector Cz(numPairs + 4, 0.0);
    F.solve(Vz, Cz);

    // `Cx/Cy/Cz` now contain the solved coefficients (e.g. for X): [w1, w2, ... wx, a0, a1x, a1y a1z]
    //
    // extract the coefficients into the return value

    TPSCoefficients3D rv;

    // populate affine a1, a2, a3, and a4 terms
    rv.a1 = {Cx[numPairs],   Cy[numPairs]  , Cz[numPairs]  };
    rv.a2 = {Cx[numPairs+1], Cy[numPairs+1], Cz[numPairs+1]};
    rv.a3 = {Cx[numPairs+2], Cy[numPairs+2], Cz[numPairs+2]};
    rv.a4 = {Cx[numPairs+3], Cy[numPairs+3], Cz[numPairs+3]};

    // populate `wi` coefficients (+ control points, needed at evaluation-time)
    rv.nonAffineTerms.reserve(numPairs);
    for (int i = 0; i < numPairs; ++i)
    {
        glm::vec3 const weight = {Cx[i], Cy[i], Cz[i]};
        glm::vec3 const& controlPoint = inputs.landmarks[i].source;
        rv.nonAffineTerms.emplace_back(weight, controlPoint);
    }

    return rv;
}

// evaluates the TPS equation with the given coefficients and input point
glm::vec3 osc::EvaluateTPSEquation(TPSCoefficients3D const& coefs, glm::vec3 p)
{
    // this implementation effectively evaluates `fx(x, y, z)`, `fy(x, y, z)`, and
    // `fz(x, y, z)` the same time, because `TPSCoefficients3D` stores the X, Y, and Z
    // variants of the coefficients together in memory (as `vec3`s)

    // compute affine terms (a1 + a2*x + a3*y + a4*z)
    glm::dvec3 rv = glm::dvec3{coefs.a1} + glm::dvec3{coefs.a2*p.x} + glm::dvec3{coefs.a3*p.y} + glm::dvec3{coefs.a4*p.z};

    // accumulate non-affine terms (effectively: wi * U(||controlPoint - p||))
    for (TPSNonAffineTerm3D const& term : coefs.nonAffineTerms)
    {
        rv += term.weight * RadialBasisFunction3D(term.controlPoint, p);
    }

    return rv;
}

// returns a mesh that is the equivalent of applying the 3D TPS warp to each vertex of the mesh
osc::Mesh osc::ApplyThinPlateWarpToMesh(TPSCoefficients3D const& coefs, osc::Mesh const& mesh)
{
    OSC_PERF("ApplyThinPlateWarpToMesh");

    osc::Mesh rv = mesh;  // make a local copy of the input mesh

    rv.transformVerts([&coefs](nonstd::span<glm::vec3> verts)
    {
        // parallelize function evaluation, because the mesh may contain *a lot* of
        // verts and the TPS equation may contain *a lot* of coefficients
        osc::ForEachParUnseq(8192, verts, [&coefs](glm::vec3& vert)
        {
            vert = EvaluateTPSEquation(coefs, vert);
        });
    });

    // TODO: come up with a more robust way to transform the normals

    return rv;
}

std::vector<glm::vec3> osc::LoadLandmarksFromCSVFile(std::filesystem::path const& p)
{
    std::vector<glm::vec3> rv;

    auto fInput = std::make_shared<std::ifstream>(p);
    if (!(*fInput))
    {
        return rv;  // some kind of error opening the file
    }
    osc::CSVReader reader{fInput};

    while (auto maybeCols = reader.next())
    {
        std::vector<std::string> cols = *maybeCols;
        if (cols.size() < 3)
        {
            continue;  // too few columns: ignore
        }
        std::optional<float> const x = osc::FromCharsStripWhitespace(cols[0]);
        std::optional<float> const y = osc::FromCharsStripWhitespace(cols[1]);
        std::optional<float> const z = osc::FromCharsStripWhitespace(cols[2]);
        if (!(x && y && z))
        {
            continue;  // a column was non-numeric: ignore entire line
        }
        rv.emplace_back(*x, *y, *z);
    }

    return rv;
}