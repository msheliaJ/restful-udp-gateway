# Local SonarQube + SonarScanner in Docker (Windows PowerShell).
# Run from the repository root:   .\tools\sonar-local\run-local-sonar.ps1 -Step server
#                                 .\tools\sonar-local\run-local-sonar.ps1 -Step scan
param(
    [ValidateSet("server", "scan")]
    [string]$Step = "server"
)

if ($Step -eq "server") {
    # Step 1: shared network so the scanner can reach http://sonarqube:9000
    docker network inspect sonar-net *> $null
    if ($LASTEXITCODE -ne 0) { docker network create sonar-net }

    # Step 2: SonarQube Community Build with persistent volumes
    docker run -d --name sonarqube --network sonar-net -p 9000:9000 `
        -e SONAR_ES_BOOTSTRAP_CHECKS_DISABLE=true `
        -v sonarqube_data:/opt/sonarqube/data `
        -v sonarqube_extensions:/opt/sonarqube/extensions `
        -v sonarqube_logs:/opt/sonarqube/logs `
        sonarqube:community

    Write-Host "SonarQube is starting. Watch: docker logs -f sonarqube"
    Write-Host "Then open http://localhost:9000 (first login admin / admin)."
    exit 0
}

# Step 5: SonarScanner (custom image with the C++ toolchain)
if (-not $env:SONAR_TOKEN) {
    Write-Error 'Set the token first:  $env:SONAR_TOKEN="<your local token>"'
    exit 1
}

docker build -t gateway-sonar-scanner -f tools/sonar-local/Dockerfile.scanner tools/sonar-local
docker run --rm --network sonar-net `
    -e SONAR_HOST_URL=http://sonarqube:9000 `
    -e SONAR_TOKEN `
    -v "${PWD}:/usr/src" `
    gateway-sonar-scanner
