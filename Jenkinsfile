pipeline {
    agent any    
    environment {
        DOCKERHUB_CREDENTIALS = 'DockerHubCred' 
        IMAGE_NAME = 'aayanksinghai/auction-system'
        IMAGE_TAG = "${env.BUILD_NUMBER}"
    }

    stages {
        stage('Checkout') {
            steps {
                git branch: 'main', 
                    credentialsId: 'github_credentials', 
                    url: 'https://github.com/aayanksinghai/Real-Time-Online-Auction-System'
            }
        }
        
        stage('Build Image') {
            steps {
                script {
                    echo "Building Docker Image: ${IMAGE_NAME}:${IMAGE_TAG}"
                    // The Dockerfile compiles the code. If it fails to compile, the build fails here.
                    dockerImage = docker.build("${IMAGE_NAME}:${IMAGE_TAG}")
                }
            }
        }
        
        stage('Push to DockerHub') {
            steps {
                script {
                    echo "Pushing Image to DockerHub..."
                    docker.withRegistry('https://index.docker.io/v1/', "${DOCKERHUB_CREDENTIALS}") {
                        dockerImage.push()
                        // Tag it as latest as well
                        dockerImage.push('latest')
                    }
                }
            }
        }
    }
    
    post {
        success {
            echo "Pipeline completed successfully! Image pushed to DockerHub."
        }
        failure {
            echo "Pipeline failed. Check the logs for compilation or Docker errors."
        }
    }
}